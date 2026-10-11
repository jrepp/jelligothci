#include "debug_socket.h"
#include "sound_output.h"
#include "jelli/debug.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int listener = -1, client = -1;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static JelliDebug debug;
static char received[64];
static size_t received_size, received_at, sent;
static uint64_t pending_since;

static bool nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void disconnect_client(JelliPetEngine *engine, uint64_t now)
{
    if (client >= 0)
        (void)close(client);
    client = -1;
    if (debug.captured)
        engine->last_ms = now;
    debug = (JelliDebug){.capture_id = debug.capture_id, .sound = jelli_sdl_sound_request};
    received_size = received_at = sent = 0;
}

void jelli_sdl_debug_close(void)
{
    if (client >= 0)
        (void)close(client);
    if (listener >= 0)
        (void)close(listener);
    client = listener = -1;
    if (socket_path[0]) {
        (void)unlink(socket_path);
        socket_path[0] = '\0';
    }
}

bool jelli_sdl_debug_open(const char *path)
{
    debug.sound = jelli_sdl_sound_request;
    if (!path)
        return true;
    if (!*path || strlen(path) >= sizeof(socket_path)) {
        fprintf(stderr, "Debug socket path is empty or too long\n");
        return false;
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path, path, strlen(path) + 1u);
    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0 || !nonblocking(listener))
        goto fail;
    /* Never unlink an existing path, including another running instance. */
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0)
        goto fail;
    memcpy(socket_path, path, strlen(path) + 1u);
    if (chmod(path, S_IRUSR | S_IWUSR) < 0 || listen(listener, 1) < 0)
        goto fail;
    fprintf(stderr, "Local debug socket: %s\n", path);
    return true;
fail:
    fprintf(stderr, "Cannot open debug socket %s: %s\n", path, strerror(errno));
    jelli_sdl_debug_close();
    return false;
}

static void accept_client(void)
{
    int accepted = accept(listener, NULL, NULL);
    if (accepted < 0)
        return;
    if (!nonblocking(accepted)) {
        (void)close(accepted);
        return;
    }
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    if (setsockopt(accepted, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        (void)close(accepted);
        return;
    }
#endif
    client = accepted;
}

static bool read_request(JelliPetEngine *engine, uint64_t now)
{
    if (received_at == received_size) {
        ssize_t size = recv(client, received, sizeof(received), 0);
        if (size == 0)
            return false;
        if (size < 0)
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        received_size = (size_t)size;
        received_at = 0;
    }
    for (unsigned n = 0; n < sizeof(received) && received_at < received_size; ++n) {
        jelli_debug_feed(&debug, engine, received[received_at++], now);
        if (debug.protocol.reply_size) {
            pending_since = now;
            break;
        }
    }
    return true;
}

static bool write_reply(uint64_t now)
{
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    ssize_t size =
        send(client, debug.protocol.reply + sent, debug.protocol.reply_size - sent, flags);
    if (size > 0) {
        sent += (size_t)size;
        if (sent == debug.protocol.reply_size) {
            sent = 0;
            debug.protocol.reply_size = 0;
        }
    } else if (size == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        return false;
    }
    return now >= pending_since && now - pending_since < 2000u;
}

static void drain_capture_input(JelliPetEngine *engine)
{
    JelliInput input;
    for (unsigned n = 0; n < 32u && engine->platform.poll; ++n) {
        if (!engine->platform.poll(engine->platform.ctx, &input))
            break;
        if (input.kind == JELLI_QUIT)
            engine->running = false;
    }
}

bool jelli_sdl_debug_poll(JelliPetEngine *engine)
{
    if (listener < 0)
        return false;
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    bool was_captured = debug.captured;
    (void)jelli_debug_frozen(&debug, engine, now);
    if (client < 0)
        accept_client();
    if (client >= 0) {
        bool ok = debug.protocol.reply_size ? write_reply(now) : read_request(engine, now);
        if (!ok)
            disconnect_client(engine, now);
    }
    if (was_captured || debug.captured)
        drain_capture_input(engine);
    return jelli_debug_frozen(&debug, engine, now);
}

#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* 512-byte requests accommodate hex-encoded device settings; 128 bytes cover metadata. */
_Static_assert(sizeof(JelliDebug) <= JELLI_DEBUG_LINE + JELLI_DEBUG_REPLY + 128u,
               "Debug state exceeds budget");

void jelli_debug_response(JelliDebug *debug, uint32_t id, const char *body)
{
    jelli_debug_protocol_response(&debug->protocol, id, body);
}

bool jelli_debug_frozen(JelliDebug *debug, JelliPetEngine *engine, uint64_t now)
{
    if (debug->captured) {
        engine->last_ms = now;
        if (now < debug->capture_activity || now - debug->capture_activity >= JELLI_DEBUG_IDLE_MS ||
            now < debug->capture_start || now - debug->capture_start >= JELLI_DEBUG_CAPTURE_MS)
            debug->captured = false;
    }
    return debug->captured;
}

static void pixels(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id, uint32_t offset,
                   uint32_t count)
{
    if (!count || count > JELLI_WIDTH || offset >= JELLI_WIDTH * JELLI_HEIGHT ||
        count > JELLI_WIDTH * JELLI_HEIGHT - offset) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"range\"}");
        return;
    }
    int size = snprintf(debug->protocol.reply, sizeof(debug->protocol.reply),
                        "\n@J1 %" PRIu32 " {\"ok\":true,\"capture\":%" PRIu32 ",\"offset\":%" PRIu32
                        ",\"rgb565\":\"",
                        id, debug->capture_id, offset);
    if (size < 0 || (size_t)size + (size_t)count * 4u + 4u >= sizeof(debug->protocol.reply))
        return;
    size_t used = (size_t)size;
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t pixel = offset + i;
        uint16_t color =
            engine->surface
                .pixels[(pixel / JELLI_WIDTH) * engine->surface.stride + pixel % JELLI_WIDTH];
        for (unsigned shift = 16; shift > 0; shift -= 4)
            debug->protocol.reply[used++] = hex[((uint32_t)color >> (shift - 4u)) & 15u];
    }
    memcpy(debug->protocol.reply + used, "\"}\n", 4u);
    debug->protocol.reply_size = used + 3u;
}

static void capture_command(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                            unsigned count, uint64_t now)
{
    uint32_t token = 0, offset = 0, length = 0;
    if (!debug->captured || count < 4u || !jelli_debug_number(words[3], &token) ||
        token != debug->capture_id) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"capture_expired\"}");
        return;
    }
    if (!strcmp(words[2], "release") && count == 4u) {
        engine->last_ms = now;
        debug->captured = false;
        jelli_debug_response(debug, id, "{\"ok\":true}");
    } else if (!strcmp(words[2], "pixels") && count == 6u &&
               jelli_debug_number(words[4], &offset) && jelli_debug_number(words[5], &length)) {
        debug->capture_activity = now;
        pixels(debug, engine, id, offset, length);
    } else {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax\"}");
    }
}

static void tap(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                unsigned count)
{
    uint32_t x = 0, y = 0;
    if (count != 5u || !jelli_debug_number(words[3], &x) || !jelli_debug_number(words[4], &y) ||
        x >= JELLI_WIDTH || y >= JELLI_HEIGHT) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"range\"}");
        return;
    }
    if (debug->captured || engine->game.resuming) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    jelli_pet_ui_tap(&engine->ui, &engine->game, (int)x, (int)y);
    jelli_debug_response(debug, id, "{\"ok\":true,\"input\":\"delivered\"}");
}

static void swipe(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                  unsigned count)
{
    uint32_t direction;
    static const int deltas[4][2] = {{0, -80}, {0, 80}, {-80, 0}, {80, 0}};
    if (count != 4u || !jelli_debug_number(words[3], &direction) || direction >= 4u) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"range\"}");
        return;
    }
    if (debug->captured || engine->game.resuming) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    jelli_pet_ui_swipe(&engine->ui, &engine->game, deltas[direction][0], deltas[direction][1]);
    jelli_debug_response(debug, id, "{\"ok\":true,\"input\":\"delivered\"}");
}

static void press(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                  unsigned count)
{
    uint32_t page = 0, item = 0;
    if (count != 5u || !jelli_debug_number(words[3], &page) ||
        !jelli_debug_number(words[4], &item) || page >= JELLI_UI_PAGE_COUNT || item > 9u) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"range\"}");
        return;
    }
    if (debug->captured || engine->game.resuming || page != (uint32_t)engine->ui.page ||
        (item && engine->ui.last_view.ring_moving)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy_or_page_changed\"}");
        return;
    }
    JelliPetUiButton button;
    if (!jelli_pet_ui_control(&engine->ui, item, engine->game.pets[engine->game.active].asleep,
                              &button)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"menu_closed\"}");
        return;
    }
    jelli_pet_ui_tap(&engine->ui, &engine->game, (int)(button.bounds.x + button.bounds.width / 2u),
                     (int)(button.bounds.y + button.bounds.height / 2u));
    jelli_debug_response(debug, id, "{\"ok\":true,\"input\":\"delivered\"}");
}

static bool input_command(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                          unsigned count)
{
    if (!strcmp(words[2], "press"))
        press(debug, engine, id, words, count);
    else if (!strcmp(words[2], "swipe"))
        swipe(debug, engine, id, words, count);
    else if (!strcmp(words[2], "tap"))
        tap(debug, engine, id, words, count);
    else
        return false;
    return true;
}

static bool history_command(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                            unsigned count)
{
    if (!strcmp(words[2], "clock"))
        jelli_debug_clock(debug, engine, id, words, count);
    else if (!strcmp(words[2], "habits") || !strcmp(words[2], "sleep-log"))
        jelli_debug_habits(debug, engine, id, words[2], count);
    else
        return false;
    return true;
}

static void dispatch_command(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                             unsigned count, uint64_t now)
{
    if (count == 3u && !strcmp(words[2], "capabilities")) {
        jelli_debug_response(debug, id,
                             "{\"ok\":true,\"protocol\":1,\"profile\":\"game\",\"commands\":"
                             "[" JELLI_DEBUG_GAME_COMMANDS_JSON "]}");
        return;
    }
    if (history_command(debug, engine, id, words, count))
        return;
    if (!strcmp(words[2], "state") && count == 3u) {
        jelli_debug_state(debug, engine, id);
    } else if (!strcmp(words[2], "tunables") || !strcmp(words[2], "tune")) {
        jelli_debug_tunables(debug, engine, id, words, count);
    } else if (!strcmp(words[2], "capture") && count == 3u) {
        if (debug->captured || !engine->ui.rendered) {
            jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
            return;
        }
        debug->captured = true;
        debug->capture_start = now;
        debug->capture_activity = now;
        if (++debug->capture_id == 0u)
            ++debug->capture_id;
        jelli_debug_state(debug, engine, id);
    } else if (!strcmp(words[2], "cheat")) {
        jelli_debug_cheat(debug, engine, id, words, count);
    } else if (!strcmp(words[2], "events")) {
        jelli_debug_events(debug, engine, id, words, count);
    } else if (!strcmp(words[2], "sound")) {
        jelli_debug_sound(debug, id, words, count);
    } else if (input_command(debug, engine, id, words, count)) {
        /* Input handler owns the reply. */
    } else if (!strcmp(words[2], "pixels") || !strcmp(words[2], "release")) {
        capture_command(debug, engine, id, words, count, now);
    } else {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax\"}");
    }
}

typedef struct {
    JelliDebug *debug;
    JelliPetEngine *engine;
    uint64_t now;
} DispatchContext;

static void dispatch(void *ctx, uint32_t id, char **words, unsigned count)
{
    DispatchContext *c = ctx;
    if (c->debug->command &&
        c->debug->command(c->debug->command_ctx, c->debug, c->engine, id, words, count))
        return;
    dispatch_command(c->debug, c->engine, id, words, count, c->now);
}

void jelli_debug_feed(JelliDebug *debug, JelliPetEngine *engine, char byte, uint64_t now)
{
    if (debug->protocol.reply_size)
        return;
    (void)jelli_debug_frozen(debug, engine, now);
    DispatchContext context = {debug, engine, now};
    jelli_debug_protocol_feed(&debug->protocol, byte, dispatch, &context);
}

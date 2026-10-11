#include "jelli/debug_protocol.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "line %d\n", __LINE__);                                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static JelliDebugProtocol protocol;
static unsigned calls;
static void dispatch(void *ctx, uint32_t id, char **words, unsigned count)
{
    ++calls;
    jelli_debug_protocol_response(ctx, id,
                                  count == 4u && !strcmp(words[2], "factory") &&
                                          !strcmp(words[3], "status")
                                      ? "{\"ok\":true}"
                                      : "{\"ok\":false}");
}
static void feed(const char *text)
{
    while (*text)
        jelli_debug_protocol_feed(&protocol, *text++, dispatch, &protocol);
}
int main(void)
{
    feed("@J1 4294967295 factory status\r\n");
    CHECK(calls == 1u && strstr(protocol.reply, "@J1 4294967295 {\"ok\":true}"));
    feed("@J1 2 factory status\n");
    CHECK(calls == 1u); /* Never overwrite an outstanding reply. */
    protocol.reply_size = 0;
    feed("@J1 4294967296 factory status\n@JF1 run\n@J1 3 a b c d e f\n");
    CHECK(calls == 1u);
    for (unsigned i = 0; i < JELLI_DEBUG_LINE + 10u; ++i)
        jelli_debug_protocol_feed(&protocol, 'x', dispatch, &protocol);
    feed("@J1 3 factory status\n");
    CHECK(calls == 1u);
    feed("@J1 4 factory status\n");
    CHECK(calls == 2u && strstr(protocol.reply, "@J1 4"));
    for (size_t i = 0; i < sizeof(protocol.line); ++i)
        CHECK(protocol.line[i] == 0);
    protocol.reply_size = 0;
    feed("@J1 5 factory\001 status\n@J1 6 factory status\n");
    CHECK(calls == 3u && strstr(protocol.reply, "@J1 6"));
    return 0;
}

#include "jelli/debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static unsigned calls;
static size_t length;

static bool command(void *ctx, JelliDebug *debug, const JelliPetEngine *engine, uint32_t id,
                    char **words, unsigned count)
{
    CHECK(ctx == &calls);
    (void)engine;
    if (strcmp(words[2], "host") != 0)
        return false;
    unsigned *counter = ctx;
    ++*counter;
    length = count == 4u ? strlen(words[3]) : 0;
    jelli_debug_response(debug, id, "{\"ok\":true}");
    return true;
}

static void feed(JelliDebug *debug, JelliPetEngine *engine, const char *text)
{
    debug->reply_size = 0;
    for (size_t i = 0; text[i]; ++i)
        jelli_debug_feed(debug, engine, text[i], 100u);
}

int main(void)
{
    JelliDebug debug = {.command = command, .command_ctx = &calls};
    JelliPetEngine engine = {0};
    feed(&debug, &engine, "@J1 42 host secret\n");
    CHECK(calls == 1u && length == 6u && strstr(debug.reply, "@J1 42"));
    for (size_t i = 0; i < sizeof(debug.line); ++i)
        CHECK(!debug.line[i]);
    char text[JELLI_DEBUG_LINE + 32u];
    memcpy(text, "@J1 43 host ", 12u);
    memset(text + 12u, 'a', 382u);
    memcpy(text + 394u, "\n", 2u);
    feed(&debug, &engine, text);
    CHECK(calls == 2u && length == 382u);
    memset(text + 12u, 'a', JELLI_DEBUG_LINE);
    memcpy(text + 12u + JELLI_DEBUG_LINE, "\n", 2u);
    feed(&debug, &engine, text);
    CHECK(calls == 2u && !debug.reply_size);
    feed(&debug, &engine, "@J1 44 clock\n");
    CHECK(calls == 2u && strstr(debug.reply, "clock_known"));
    feed(&debug, &engine, "@J1 45 unknown\n");
    CHECK(strstr(debug.reply, "syntax"));
    puts("Host extension handles long settings, scrubs requests, and preserves core dispatch.");
    return 0;
}

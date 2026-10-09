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

static unsigned requests;
static bool agent_present = true;

static bool switch_app(void *ctx)
{
    CHECK(ctx == &requests);
    unsigned *counter = ctx;
    ++*counter;
    return agent_present;
}

static void feed_at(JelliDebug *debug, JelliPetEngine *engine, const char *line, uint64_t now)
{
    for (const char *p = line; *p; ++p)
        jelli_debug_feed(debug, engine, *p, now);
}

static void send(JelliDebug *debug, JelliPetEngine *engine, const char *line, const char *expected)
{
    debug->reply_size = 0;
    feed_at(debug, engine, line, 100u);
    CHECK(debug->reply_size > 0u && strstr(debug->reply, expected));
}

/* Rejected requests never reach the host and leave the checkpoint request exactly as
 * it was, whether or not the game had already asked for one. */
static void rejects(JelliDebug *debug, JelliPetEngine *engine, const char *line,
                    const char *expected)
{
    for (unsigned pending = 0u; pending < 2u; ++pending) {
        unsigned before = requests;
        engine->ui.save_requested = pending != 0u;
        send(debug, engine, line, expected);
        CHECK(requests == before && engine->ui.save_requested == (pending != 0u));
    }
}

/* Lines the pet's feed never dispatches: no reply, no host call, no checkpoint change. */
static void ignores(JelliDebug *debug, JelliPetEngine *engine, const char *line)
{
    for (unsigned pending = 0u; pending < 2u; ++pending) {
        unsigned before = requests;
        engine->ui.save_requested = pending != 0u;
        debug->reply_size = 0u;
        feed_at(debug, engine, line, 100u);
        CHECK(debug->reply_size == 0u && requests == before);
        CHECK(engine->ui.save_requested == (pending != 0u));
    }
}

static void ignores_undispatched_lines(JelliDebug *debug, JelliPetEngine *engine)
{
    ignores(debug, engine, "@J1 x app agent\n");          /* id is not decimal */
    ignores(debug, engine, "@J1 4294967296 app agent\n"); /* id overflows */
    ignores(debug, engine, "@J1 -1 app agent\n");         /* signed id */
    ignores(debug, engine, "@A1 1 app agent\n");          /* the agent's own tag */
    ignores(debug, engine, "@J1 1 app\tagent\n");         /* control byte */
    ignores(debug, engine, "@J1 1 app agent a b c d\n");  /* more words than the feed keeps */
    /* An oversized line is discarded through its LF even when it ends in app agent. */
    char line[JELLI_DEBUG_LINE + 32u];
    memset(line, ' ', sizeof(line));
    memcpy(line, "@J1 1", 5u);
    memcpy(line + sizeof(line) - 11u, "app agent\n", 11u);
    ignores(debug, engine, line);
}

/* One outstanding request: while a reply waits for the host, bytes are dropped whole,
 * so a queued switch can never run behind an unsent reply. */
static void a_pending_reply_blocks_the_switch(JelliDebug *debug, JelliPetEngine *engine)
{
    unsigned before = requests;
    engine->ui.save_requested = false;
    debug->reply_size = 0u;
    feed_at(debug, engine, "@J1 20 state\n", 100u);
    size_t pending = debug->reply_size;
    CHECK(pending > 0u && strstr(debug->reply, "\n@J1 20 ") == debug->reply);
    feed_at(debug, engine, "@J1 21 app agent\n", 100u);
    CHECK(debug->reply_size == pending && strstr(debug->reply, "\n@J1 20 ") == debug->reply);
    CHECK(requests == before && !engine->ui.save_requested);
    /* After the host sends it, the next request runs normally. */
    debug->reply_size = 0u;
    feed_at(debug, engine, "@J1 22 app agent\n", 100u);
    CHECK(requests == before + 1u && engine->ui.save_requested);
    CHECK(!strcmp(debug->reply, "\n@J1 22 {\"ok\":true,\"app\":\"agent\",\"restart\":true}\n"));
}

/* Extra spaces and a CRLF ending are framing, not syntax; an expired capture no longer
 * holds the switch because the feed releases it before dispatch. */
static void tolerant_framing_and_expired_capture(JelliDebug *debug, JelliPetEngine *engine)
{
    unsigned before = requests;
    engine->ui.save_requested = false;
    send(debug, engine, "  @J1   30  app   agent  \r\n", "\n@J1 30 {\"ok\":true");
    CHECK(requests == before + 1u && engine->ui.save_requested);
    debug->captured = true;
    debug->capture_start = debug->capture_activity = 0u;
    engine->ui.save_requested = false;
    debug->reply_size = 0u;
    feed_at(debug, engine, "@J1 31 app agent\n", JELLI_DEBUG_IDLE_MS + 1u);
    CHECK(!debug->captured && requests == before + 2u && engine->ui.save_requested);
    CHECK(strstr(debug->reply, "\"restart\":true") != NULL);
}

int main(void)
{
    JelliDebug debug = {0};
    JelliPetEngine engine = {0};
    jelli_game_init(&engine.game);
    /* Hosts without an app switch (SDL) refuse without side effects. */
    rejects(&debug, &engine, "@J1 1 app agent\n", "app_unavailable");
    debug.switch_app = switch_app;
    debug.switch_app_ctx = &requests;
    rejects(&debug, &engine, "@J1 2 app\n", "app_syntax");
    rejects(&debug, &engine, "@J1 3 app pet\n", "app_syntax");
    rejects(&debug, &engine, "@J1 4 app agent now\n", "app_syntax");
    rejects(&debug, &engine, "@J1 5 app Agent\n", "app_syntax");
    /* A capture or an offline catch-up would skip the final checkpoint. */
    debug.captured = true;
    debug.capture_start = debug.capture_activity = 100u;
    rejects(&debug, &engine, "@J1 6 app agent\n", "busy");
    debug.captured = false;
    engine.game.resuming = true;
    rejects(&debug, &engine, "@J1 7 app agent\n", "busy");
    engine.game.resuming = false;
    /* The host found no agent image: the pet keeps running and saves nothing extra. */
    agent_present = false;
    engine.ui.save_requested = false;
    send(&debug, &engine, "@J1 8 app agent\n", "agent_missing");
    CHECK(requests == 1u && !engine.ui.save_requested);
    /* Accepted: exactly one host call, a checkpoint request, and a restart notice. */
    agent_present = true;
    send(&debug, &engine, "@J1 9 app agent\n", "\"restart\":true");
    CHECK(requests == 2u && engine.ui.save_requested);
    CHECK(strstr(debug.reply, "\n@J1 9 {\"ok\":true,\"app\":\"agent\"") == debug.reply);
    /* The exact refusal bodies are part of the debug protocol. */
    send(&debug, &engine, "@J1 10 app\n", "x");
    CHECK(!strcmp(debug.reply, "\n@J1 10 {\"ok\":false,\"error\":\"app_syntax\"}\n"));
    ignores_undispatched_lines(&debug, &engine);
    a_pending_reply_blocks_the_switch(&debug, &engine);
    tolerant_framing_and_expired_capture(&debug, &engine);
    /* A false host answer after successes still refuses and keeps any pending save. */
    agent_present = false;
    for (unsigned pending = 0u; pending < 2u; ++pending) {
        unsigned before = requests;
        engine.ui.save_requested = pending != 0u;
        send(&debug, &engine, "@J1 40 app agent\n", "agent_missing");
        CHECK(requests == before + 1u && engine.ui.save_requested == (pending != 0u));
    }
    puts("Debug app switch checks passed");
    return 0;
}

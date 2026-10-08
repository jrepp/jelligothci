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
static unsigned received_cue, received_volume, requests;
static bool accept = true;

static bool sound(void *ctx, unsigned cue, unsigned volume)
{
    CHECK(ctx == &requests);
    unsigned *counter = ctx;
    ++*counter;
    received_cue = cue;
    received_volume = volume;
    return accept;
}

static void send(JelliDebug *debug, JelliPetEngine *engine, const char *line, const char *expected)
{
    debug->reply_size = 0;
    for (const char *p = line; *p; ++p)
        jelli_debug_feed(debug, engine, *p, 100u);
    CHECK(debug->reply_size > 0u && strstr(debug->reply, expected));
}

int main(void)
{
    JelliDebug debug = {0};
    JelliPetEngine engine = {0};
    jelli_game_init(&engine.game);
    send(&debug, &engine, "@J1 1 sound 3 35\n", "sound_unavailable");
    debug.sound = sound;
    debug.sound_ctx = &requests;
    send(&debug, &engine, "@J1 2 sound 3 35\n", "queued");
    CHECK(requests == 1u && received_cue == 3u && received_volume == 35u);
    engine.game.sound_level = 0u; /* The explicit command ignores the Settings level. */
    send(&debug, &engine, "@J1 8 sound 3 35\n", "queued");
    CHECK(requests == 2u && received_cue == 3u && received_volume == 35u);
    engine.game.sound_level = JELLI_SOUND_LEVEL_MAX;
    send(&debug, &engine, "@J1 9 sound 5 80\n", "queued");
    CHECK(requests == 3u && received_cue == 5u && received_volume == 80u);
    send(&debug, &engine, "@J1 3 sound 7 35\n", "sound_range");
    send(&debug, &engine, "@J1 4 sound 3 81\n", "sound_range");
    send(&debug, &engine, "@J1 5 sound 3\n", "sound_range");
    CHECK(requests == 3u);
    debug.captured = true;
    debug.capture_start = debug.capture_activity = 100u;
    send(&debug, &engine, "@J1 6 sound 3 35\n", "busy");
    CHECK(requests == 3u);
    debug.captured = false;
    accept = false;
    send(&debug, &engine, "@J1 7 sound 0 0\n", "sound_busy_or_unavailable");
    CHECK(requests == 4u && received_volume == 0u);
    /* Every cue 0..8 x volume 0..90 at every Settings level: the explicit command
     * queues exactly what it names when cue < 7 and volume <= 80, and never consults
     * or changes the level. */
    accept = true;
    char line[48];
    for (unsigned cue = 0u; cue <= JELLI_SOUND_COUNT + 1u; ++cue)
        for (unsigned volume = 0u; volume <= JELLI_SOUND_VOLUME_MAX + 10u; ++volume) {
            uint8_t level = (uint8_t)((cue * 7u + volume) % (JELLI_SOUND_LEVEL_MAX + 1u));
            engine.game.sound_level = level;
            unsigned before = requests;
            CHECK(snprintf(line, sizeof(line), "@J1 10 sound %u %u\n", cue, volume) > 0);
            bool valid = cue < JELLI_SOUND_COUNT && volume <= JELLI_SOUND_VOLUME_MAX;
            send(&debug, &engine, line, valid ? "queued" : "sound_range");
            CHECK(requests == before + (valid ? 1u : 0u) && engine.game.sound_level == level);
            if (valid)
                CHECK(received_cue == cue && received_volume == volume);
        }
    puts("Sound protocol validates ranges, queue rejection, missing host and capture lease.");
    return 0;
}

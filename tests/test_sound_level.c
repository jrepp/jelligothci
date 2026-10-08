/* The Settings sound level: its default, validity, and the request volume table
 * that both hosts use. The table is written out, not recomputed, so a change to the
 * mapping is a deliberate test change. */
#include "jelli/game.h"
#include "jelli/sound.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* Rows: the tap cue, then every other cue. Columns: levels 0..10. Level 5 is the
 * original fixed loudness; level 10 doubles it. */
static const unsigned tap_volumes[JELLI_SOUND_LEVEL_MAX + 1u] = {0u,  5u,  10u, 15u, 20u, 25u,
                                                                 30u, 35u, 40u, 45u, 50u};
static const unsigned voice_volumes[JELLI_SOUND_LEVEL_MAX + 1u] = {0u,  4u,  7u,  11u, 14u, 18u,
                                                                   22u, 25u, 29u, 32u, 36u};

static void full_table(void)
{
    for (unsigned cue = 0u; cue < JELLI_SOUND_COUNT; ++cue) {
        unsigned previous = 0u;
        for (unsigned level = 0u; level <= JELLI_SOUND_LEVEL_MAX; ++level) {
            unsigned volume = jelli_sound_volume(level, cue);
            const unsigned *row = cue == JELLI_SOUND_TAP ? tap_volumes : voice_volumes;
            CHECK(volume == row[level]);
            CHECK(volume <= JELLI_SOUND_VOLUME_MAX);
            CHECK(level == 0u ? volume == 0u : volume > previous); /* Every step is audible. */
            previous = volume;
        }
    }
    /* The default reproduces the hosts' former constants exactly: tap 25, others 18. */
    CHECK(jelli_sound_volume(JELLI_SOUND_LEVEL_DEFAULT, JELLI_SOUND_TAP) == 25u);
    for (unsigned cue = 0u; cue < JELLI_SOUND_TAP; ++cue)
        CHECK(jelli_sound_volume(JELLI_SOUND_LEVEL_DEFAULT, cue) == 18u);
    CHECK(jelli_sound_volume(JELLI_SOUND_LEVEL_DEFAULT, 6u) == 18u); /* Coo. */
}

static void invalid_inputs(void)
{
    static const unsigned levels[] = {JELLI_SOUND_LEVEL_MAX + 1u, 99u, 255u, 256u, UINT_MAX};
    for (unsigned i = 0u; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        /* Clamped to the maximum: never louder, never wrapping to silence. */
        CHECK(jelli_sound_volume(levels[i], JELLI_SOUND_TAP) == 50u);
        CHECK(jelli_sound_volume(levels[i], 0u) == 36u);
    }
    for (unsigned level = 0u; level <= JELLI_SOUND_LEVEL_MAX + 1u; ++level) {
        CHECK(jelli_sound_volume(level, JELLI_SOUND_COUNT) == 0u);
        CHECK(jelli_sound_volume(level, UINT_MAX) == 0u);
    }
}

static void game_default_and_validity(void)
{
    JelliGame game, fresh;
    jelli_game_init(&game);
    jelli_game_new(&fresh);
    CHECK(JELLI_SOUND_LEVEL_DEFAULT * 2u == JELLI_SOUND_LEVEL_MAX); /* The middle. */
    CHECK(game.sound_level == JELLI_SOUND_LEVEL_DEFAULT);
    CHECK(fresh.sound_level == JELLI_SOUND_LEVEL_DEFAULT && jelli_game_valid(&fresh));
    for (unsigned level = 0u; level < 256u; ++level) {
        game.sound_level = (uint8_t)level;
        CHECK(jelli_game_valid(&game) == (level <= JELLI_SOUND_LEVEL_MAX));
    }
}

/* Every level 0..255 x cue 0..255 (the range a uint8_t level or cue byte can carry):
 * never above the host cap, non-decreasing in the level for each valid cue, linear
 * with round-to-nearest, flat at the clamp, and silent for level 0 or unknown cues. */
static void exhaustive_sweep(void)
{
    for (unsigned cue = 0u; cue < 256u; ++cue) {
        bool valid = cue < JELLI_SOUND_COUNT;
        unsigned base = cue == JELLI_SOUND_TAP ? 25u : 18u, previous = 0u;
        for (unsigned level = 0u; level < 256u; ++level) {
            unsigned volume = jelli_sound_volume(level, cue);
            unsigned clamped = level > JELLI_SOUND_LEVEL_MAX ? JELLI_SOUND_LEVEL_MAX : level;
            CHECK(volume <= JELLI_SOUND_VOLUME_MAX && volume <= 2u * base);
            CHECK(volume == jelli_sound_volume(level, cue)); /* Deterministic, no state. */
            if (!valid || level == 0u) {
                CHECK(volume == 0u);
                continue;
            }
            /* |volume - base * level / 5| <= 1/2, in integers: */
            unsigned scaled = base * clamped, twice = 2u * volume * JELLI_SOUND_LEVEL_DEFAULT;
            CHECK(twice + JELLI_SOUND_LEVEL_DEFAULT >= 2u * scaled &&
                  twice <= 2u * scaled + JELLI_SOUND_LEVEL_DEFAULT);
            CHECK(volume >= previous && volume > 0u);
            if (level > JELLI_SOUND_LEVEL_MAX)
                CHECK(volume == jelli_sound_volume(JELLI_SOUND_LEVEL_MAX, cue));
            previous = volume;
        }
    }
}

int main(void)
{
    full_table();
    invalid_inputs();
    exhaustive_sweep();
    game_default_and_validity();
    puts("PASS: sound level default 5, validity 0..10, and the full cue x level volume table");
    return 0;
}

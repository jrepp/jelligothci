#ifndef JELLI_TEST_GAME_GEN_H
#define JELLI_TEST_GAME_GEN_H

#include "jelli/game.h"

/* Seeded xorshift state; a nonzero seed replays the same games. */
typedef struct {
    uint32_t state;
} GameGen;

uint32_t game_gen_next(GameGen *g);
/* Uniform-ish value in [0, bound); bound must be nonzero. */
uint32_t game_gen_below(GameGen *g, uint32_t bound);
/* Builds a game jelli_game_valid accepts, biased toward rule thresholds: healthy, unwell,
 * and recovering pets on or beside the 400 floor; scheduled sleep, naps, and linked manual
 * sleep; activities mid-flight; empty and full stacks; offered and owned prizes; and
 * clocks near saturation. False means the generator built an invalid game (a test bug).
 * Leaves the event sink unset and the game not resuming. */
bool game_gen_build(GameGen *g, JelliGame *game);

#endif

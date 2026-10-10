#ifndef JELLI_TEST_GAME_FIXTURE_H
#define JELLI_TEST_GAME_FIXTURE_H
#include "jelli/game.h"
/* Explicit two-owned-family fixture; new games have one starter family. */
static inline void test_game_pair(JelliGame *fixture)
{
    jelli_game_init(fixture);
    fixture->pets[1] = fixture->pets[0];
    fixture->pets[1].id = 2u;
    fixture->pets[1].collection_entry = 2u;
    fixture->pets[1].random_state = 2u;
    fixture->count = 2u;
}
#endif

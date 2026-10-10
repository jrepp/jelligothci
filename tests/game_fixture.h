#ifndef JELLI_TEST_GAME_FIXTURE_H
#define JELLI_TEST_GAME_FIXTURE_H
#include "jelli/collection.h"
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
/* Clone pet 0 into slots from..count-1 (entry = slot + 1, id = first_id + slot), each in its
 * entry's starting form. */
static inline void test_game_fill(JelliGame *fixture, unsigned from, unsigned count,
                                  uint32_t first_id)
{
    for (unsigned i = from; i < count; ++i) {
        fixture->pets[i] = fixture->pets[0];
        fixture->pets[i].id = first_id + i;
        fixture->pets[i].collection_entry = (uint8_t)(i + 1u);
    }
    fixture->count = (uint8_t)count;
    jelli_collection_normalize(fixture);
}
#endif

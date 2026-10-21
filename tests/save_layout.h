#ifndef JELLI_TEST_SAVE_LAYOUT_H
#define JELLI_TEST_SAVE_LAYOUT_H
#include "jelli/prizes.h"
#include <stddef.h>
/* Per-pet bytes at the end of a save, newest version last; legacy-save tests strip them. */
#define TEST_SAVE_V9_PET_BYTES 4u  /* rest_ticks */
#define TEST_SAVE_V10_PET_BYTES 5u /* moment, digesting, potty */
#define TEST_SAVE_V11_PET_BYTES                                                                    \
    7u /* behavior, behavior_left, cooldown_state/left, behavior_flags */
#define TEST_SAVE_V12_PET_BYTES 6u /* activity_day, completed_moments */
#define TEST_SAVE_TAIL_PET_BYTES                                                                   \
    (TEST_SAVE_V9_PET_BYTES + TEST_SAVE_V10_PET_BYTES + TEST_SAVE_V11_PET_BYTES +                  \
     TEST_SAVE_V12_PET_BYTES)
/* Current tail: v14 adds slot 9, v15 adds the remaining bounded prize origins. */
#define TEST_SAVE_V14_GAME_BYTES ((size_t)4u * (JELLI_PRIZE_V16_COUNT - JELLI_PRIZE_LEGACY_COUNT))
/* From version 4: collection binding 2, hydration 4, food type 1, then the tail above. */
#define TEST_SAVE_V4_PET_BYTES (7u + TEST_SAVE_TAIL_PET_BYTES)
/* v16: schema2 + session34 bytes +35 bytes per pet. */
#define TEST_SAVE_V18_BYTES 53u
#define TEST_SAVE_V17_BYTES(count) (TEST_SAVE_V18_BYTES + 20u + (size_t)(count) * 24u)
#define TEST_SAVE_V16_BYTES(count) (35u + (size_t)(count) * 35u + TEST_SAVE_V17_BYTES(count))
#define TEST_SAVE_NEW_GAME_BYTES(count) (TEST_SAVE_V14_GAME_BYTES + TEST_SAVE_V16_BYTES(count))
#endif

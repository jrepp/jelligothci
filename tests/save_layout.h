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
/* After the per-pet tail, version 14 appends origin pets for prizes past the legacy nine. */
#define TEST_SAVE_V14_GAME_BYTES ((size_t)4u * (JELLI_PRIZE_COUNT - JELLI_PRIZE_LEGACY_COUNT))
/* From version 4: collection binding 2, hydration 4, food type 1, then the tail above. */
#define TEST_SAVE_V4_PET_BYTES (7u + TEST_SAVE_TAIL_PET_BYTES)
#endif

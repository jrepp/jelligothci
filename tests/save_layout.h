#ifndef JELLI_TEST_SAVE_LAYOUT_H
#define JELLI_TEST_SAVE_LAYOUT_H
/* Per-pet bytes at the end of a save, newest version last; legacy-save tests strip them. */
#define TEST_SAVE_V9_PET_BYTES 4u  /* rest_ticks */
#define TEST_SAVE_V10_PET_BYTES 5u /* moment, digesting, potty */
#define TEST_SAVE_TAIL_PET_BYTES (TEST_SAVE_V9_PET_BYTES + TEST_SAVE_V10_PET_BYTES)
/* From version 4: collection binding 2, hydration 4, food type 1, then the tail above. */
#define TEST_SAVE_V4_PET_BYTES (7u + TEST_SAVE_TAIL_PET_BYTES)
#endif

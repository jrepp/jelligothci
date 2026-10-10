#ifndef JELLI_TESTS_PREFS_BMP_H
#define JELLI_TESTS_PREFS_BMP_H

#include <stdbool.h>
#include <stdint.h>

/* Writes folder/name.bmp: a side by side RGB565 screen (stride in pixels) as a 24-bit
 * bottom-up BMP, with the corners outside the round panel grey, as it looks. False
 * when the file cannot be written whole. For the Settings previews (ADR-016). */
bool prefs_bmp_save(const char *folder, const char *name, const uint16_t *pixels, unsigned side,
                    unsigned stride);

#endif

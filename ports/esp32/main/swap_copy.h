#ifndef JELLI_SWAP_COPY_H
#define JELLI_SWAP_COPY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Nonoverlapping buffers contain at least pixels * 2 bytes. Accept arbitrary
 * byte alignment; memcpy keeps word operations free of aliasing/alignment UB.
 * Callers validate the pixel count against their bounded buffer capacity. */
static inline void jelli_swap_copy(uint8_t *dest, const uint8_t *source, size_t pixels)
{
    while (pixels >= 2u) {
        uint32_t value;
        memcpy(&value, source, sizeof(value));
        value = ((value & UINT32_C(0x00ff00ff)) << 8u) | ((value & UINT32_C(0xff00ff00)) >> 8u);
        memcpy(dest, &value, sizeof(value));
        source += sizeof(value);
        dest += sizeof(value);
        pixels -= 2u;
    }
    if (pixels) {
        dest[0] = source[1];
        dest[1] = source[0];
    }
}
#endif

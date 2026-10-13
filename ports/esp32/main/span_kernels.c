#include "span_kernels.h"

/* Assembly accepts aligned destination, positive block count, aligned pattern. */
void jelli_span_fill_blocks(uint16_t *dest, size_t blocks, const uint16_t *pattern);

uint16_t jelli_span_blend_pixel(uint16_t back, uint16_t front, unsigned alpha)
{
    unsigned inverse = 16u - alpha;
    unsigned r = (((back >> 11) & 31u) * inverse + ((front >> 11) & 31u) * alpha) / 16u;
    unsigned g = (((back >> 5) & 63u) * inverse + ((front >> 5) & 63u) * alpha) / 16u;
    unsigned b = ((back & 31u) * inverse + (front & 31u) * alpha) / 16u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void jelli_span_fill_scalar(uint16_t *dest, size_t count, uint16_t color, unsigned alpha)
{
    (void)alpha;
    for (size_t i = 0; i < count; ++i)
        dest[i] = color;
}

void jelli_span_fill_simd(uint16_t *dest, size_t count, uint16_t color, unsigned alpha)
{
    if (count < 64u) {
        jelli_span_fill_scalar(dest, count, color, alpha);
        return;
    }
    while (count && ((uintptr_t)dest & 15u)) {
        *dest++ = color;
        --count;
    }
    if (count >= 8u) {
        _Alignas(16) uint16_t pattern[8];
        for (unsigned i = 0; i < 8u; ++i)
            pattern[i] = color;
        size_t blocks = count / 8u;
        jelli_span_fill_blocks(dest, blocks, pattern);
        dest += blocks * 8u;
        count %= 8u;
    }
    while (count--) {
        *dest++ = color;
    }
}

void jelli_span_blend_scalar(uint16_t *dest, size_t count, uint16_t color, unsigned alpha)
{
    for (size_t i = 0; i < count; ++i)
        dest[i] = jelli_span_blend_pixel(dest[i], color, alpha);
}

void jelli_span_blend_packed(uint16_t *dest, size_t count, uint16_t color, unsigned alpha)
{
    if (alpha == 0u)
        return;
    if (alpha == 16u) {
        jelli_span_fill_simd(dest, count, color, alpha);
        return;
    }
    /* Separate B, R and G into lanes with room for 4 fractional bits.
     * Weighted sum fits uint32_t and cannot carry between channel lanes. */
    const uint32_t mask = UINT32_C(0x07e0f81f);
    uint32_t front = ((uint32_t)color | ((uint32_t)color << 16)) & mask;
    unsigned inverse = 16u - alpha;
    for (size_t i = 0; i < count; ++i) {
        uint32_t back = ((uint32_t)dest[i] | ((uint32_t)dest[i] << 16)) & mask;
        uint32_t mixed = ((back * inverse + front * alpha) >> 4) & mask;
        dest[i] = (uint16_t)(mixed | (mixed >> 16));
    }
}

#ifndef JELLI_SPAN_KERNELS_H
#define JELLI_SPAN_KERNELS_H
#include <stddef.h>
#include <stdint.h>
/* Diagnostic-only RGB565 spans. Caller supplies count writable pixels;
 * alpha is in [0,16]. No clipping inside kernels; no access outside the span. */
uint16_t jelli_span_blend_pixel(uint16_t back, uint16_t front, unsigned alpha);
void jelli_span_fill_scalar(uint16_t *dest, size_t count, uint16_t color, unsigned alpha);
void jelli_span_fill_simd(uint16_t *dest, size_t count, uint16_t color, unsigned alpha);
void jelli_span_blend_scalar(uint16_t *dest, size_t count, uint16_t color, unsigned alpha);
void jelli_span_blend_packed(uint16_t *dest, size_t count, uint16_t color, unsigned alpha);
#endif

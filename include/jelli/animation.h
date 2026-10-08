#ifndef JELLI_ANIMATION_H
#define JELLI_ANIMATION_H

#include <stdint.h>

typedef struct {
    uint8_t r, g, b;
} JelliColor;

/* Caller-owned, fixed-size linear RGB transition. Zero-initialize then set value.
 * Functions require a non-NULL tween initialized here or by jelli_color_start.
 * Starting again replaces the target from the current value, without a queue.
 * Zero duration snaps immediately. Advance clamps at the exact endpoint. */
typedef struct {
    JelliColor from, to, value;
    uint32_t elapsed_ms, duration_ms;
} JelliColorTween;

void jelli_color_start(JelliColorTween *tween, JelliColor target, uint32_t duration_ms);
void jelli_color_advance(JelliColorTween *tween, uint64_t elapsed_ms);
uint16_t jelli_rgb565(JelliColor color);

#endif

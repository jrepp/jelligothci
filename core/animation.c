#include "jelli/animation.h"

void jelli_color_start(JelliColorTween *tween, JelliColor target, uint32_t duration_ms)
{
    tween->from = tween->value;
    tween->to = target;
    tween->elapsed_ms = 0;
    tween->duration_ms = duration_ms;
    if (duration_ms == 0)
        tween->value = target;
}

static uint8_t channel(uint8_t from, uint8_t to, uint32_t elapsed, uint32_t duration)
{
    /* Widen before multiplying: even UINT32_MAX duration stays within uint64_t. */
    uint64_t weighted = (uint64_t)from * (duration - elapsed) + (uint64_t)to * elapsed;
    return (uint8_t)(weighted / duration);
}

void jelli_color_advance(JelliColorTween *tween, uint64_t elapsed_ms)
{
    if (tween->elapsed_ms == tween->duration_ms)
        return;
    uint32_t remaining = tween->duration_ms - tween->elapsed_ms;
    if (elapsed_ms >= remaining) {
        tween->elapsed_ms = tween->duration_ms;
        tween->value = tween->to;
        return;
    }
    tween->elapsed_ms += (uint32_t)elapsed_ms;
    tween->value =
        (JelliColor){channel(tween->from.r, tween->to.r, tween->elapsed_ms, tween->duration_ms),
                     channel(tween->from.g, tween->to.g, tween->elapsed_ms, tween->duration_ms),
                     channel(tween->from.b, tween->to.b, tween->elapsed_ms, tween->duration_ms)};
}

uint16_t jelli_rgb565(JelliColor color)
{
    return (uint16_t)(((unsigned)color.r >> 3) << 11 | ((unsigned)color.g >> 2) << 5 |
                      ((unsigned)color.b >> 3));
}

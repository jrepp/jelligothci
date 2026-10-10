#include "jelli/agent_touch.h"
#include <stddef.h>

void jelli_agent_touch_init(JelliAgentTouch *touch)
{
    if (touch)
        *touch = (JelliAgentTouch){0};
}

/* Whether (x, y) lies beyond the slop from where the press began; 64-bit, so no
 * coordinates can overflow the difference. */
static bool moved(const JelliAgentTouch *touch, int x, int y)
{
    int64_t dx = (int64_t)x - touch->start_x, dy = (int64_t)y - touch->start_y;
    return dx < -JELLI_AGENT_TAP_SLOP || dx > JELLI_AGENT_TAP_SLOP || dy < -JELLI_AGENT_TAP_SLOP ||
           dy > JELLI_AGENT_TAP_SLOP;
}

static int clamped(int64_t value)
{
    if (value > 4096)
        return 4096;
    return value < -4096 ? -4096 : (int)value;
}

JelliAgentTouchEvent jelli_agent_touch_sample(JelliAgentTouch *touch, bool pressed, int x, int y,
                                              uint64_t now_ms)
{
    JelliAgentTouchEvent none = {JELLI_AGENT_TOUCH_NONE, 0, 0, 0};
    if (!touch)
        return none;
    if (!pressed) {
        bool tap = touch->down && !touch->dragging && now_ms >= touch->start_ms &&
                   now_ms - touch->start_ms <= JELLI_AGENT_TAP_MAX_MS;
        JelliAgentTouchEvent event = {JELLI_AGENT_TOUCH_TAP, touch->start_x, touch->start_y, 0};
        touch->down = touch->dragging = false;
        return tap ? event : none;
    }
    if (!touch->down) {
        *touch = (JelliAgentTouch){x, y, y, now_ms, true, false};
        return none;
    }
    if (!touch->dragging && !moved(touch, x, y))
        return none;
    JelliAgentTouchEvent drag = {JELLI_AGENT_TOUCH_DRAG, x, y, clamped((int64_t)y - touch->last_y)};
    if (!touch->dragging) /* The whole movement so far, once it starts. */
        drag.dy = clamped((int64_t)y - touch->start_y);
    touch->dragging = true;
    touch->last_y = y;
    return drag;
}

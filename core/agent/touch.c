#include "jelli/agent_touch.h"
#include <stddef.h>

enum { PATH_CAP = 1000000 };

void jelli_agent_touch_init(JelliAgentTouch *touch)
{
    if (touch)
        *touch = (JelliAgentTouch){0};
}

void jelli_agent_touch_holds(JelliAgentTouch *touch, bool allowed)
{
    if (touch)
        touch->holds = allowed;
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

/* The distance travelled grows by this step, across plus down, and stops at a cap. */
static void travel(JelliAgentTouch *touch, int x, int y)
{
    int dx = clamped((int64_t)x - touch->last_x), dy = clamped((int64_t)y - touch->last_y);
    uint32_t step = (uint32_t)(dx < 0 ? -dx : dx) + (uint32_t)(dy < 0 ? -dy : dy);
    touch->path = touch->path > PATH_CAP - step ? PATH_CAP : touch->path + step;
    touch->last_x = x;
    touch->last_y = y;
}

/* The finger lifted: a hold ends, a drag was a stroke, a short still press a tap. */
static JelliAgentTouchEvent lifted(JelliAgentTouch *touch, uint64_t now_ms)
{
    JelliAgentTouchEvent event = {JELLI_AGENT_TOUCH_NONE, touch->start_x, touch->start_y, 0, 0u};
    if (touch->holding)
        event.kind = JELLI_AGENT_TOUCH_RELEASE;
    else if (touch->dragging) {
        event.kind = JELLI_AGENT_TOUCH_STROKE;
        event.path = touch->path;
    } else if (touch->down && now_ms >= touch->start_ms &&
               now_ms - touch->start_ms <= JELLI_AGENT_TAP_MAX_MS)
        event.kind = JELLI_AGENT_TOUCH_TAP;
    bool holds = touch->holds;
    *touch = (JelliAgentTouch){0};
    touch->holds = holds;
    return event;
}

JelliAgentTouchEvent jelli_agent_touch_sample(JelliAgentTouch *touch, bool pressed, int x, int y,
                                              uint64_t now_ms)
{
    JelliAgentTouchEvent none = {JELLI_AGENT_TOUCH_NONE, 0, 0, 0, 0u};
    if (!touch)
        return none;
    if (!pressed)
        return touch->down ? lifted(touch, now_ms) : none;
    if (!touch->down) {
        bool holds = touch->holds;
        *touch = (JelliAgentTouch){x, y, x, y, now_ms, 0u, true, false, false, holds};
        return none;
    }
    if (touch->holding) /* A held finger may wander; it is still the same hold. */
        return none;
    if (!touch->dragging && !moved(touch, x, y)) {
        bool due = touch->holds && now_ms >= touch->start_ms &&
                   now_ms - touch->start_ms >= JELLI_AGENT_HOLD_MS;
        if (!due)
            return none;
        touch->holding = true;
        return (JelliAgentTouchEvent){JELLI_AGENT_TOUCH_HOLD, touch->start_x, touch->start_y, 0,
                                      0u};
    }
    JelliAgentTouchEvent drag = {JELLI_AGENT_TOUCH_DRAG, x, y, clamped((int64_t)y - touch->last_y),
                                 0u};
    if (!touch->dragging) /* The whole movement so far, once it starts. */
        drag.dy = clamped((int64_t)y - touch->start_y);
    touch->dragging = true;
    travel(touch, x, y);
    return drag;
}

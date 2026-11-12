#include "jelli/gesture.h"

void jelli_gesture_begin(JelliGesture *gesture, int x, int y)
{
    jelli_gesture_begin_at(gesture, x, y, 0u);
}

void jelli_gesture_begin_at(JelliGesture *gesture, int x, int y, uint64_t now_ms)
{
    *gesture = (JelliGesture){.x = x,
                              .y = y,
                              .last_x = x,
                              .last_y = y,
                              .active = x >= 0 && y >= 0 && x < JELLI_WIDTH && y < JELLI_HEIGHT,
                              .started_ms = now_ms};
}

bool jelli_gesture_move(JelliGesture *gesture, int x, int y)
{
    if (!gesture->active)
        return false;
    gesture->last_x = x;
    gesture->last_y = y;
    if (gesture->moved)
        return false;
    if (x < gesture->x - 18 || x > gesture->x + 18 || y < gesture->y - 18 || y > gesture->y + 18) {
        gesture->moved = true;
        return true;
    }
    return false;
}

bool jelli_gesture_end(JelliGesture *gesture, int x, int y, JelliInput *input)
{
    return jelli_gesture_end_at(gesture, x, y, gesture->started_ms, input);
}

static bool swipe_input(int dx, int dy, int ax, int ay, JelliInput *input)
{
    /* Dead band and 3:2 dominance reject ambiguous drags and diagonals. */
    if ((ax >= 48 && ax * 2 >= ay * 3) || (ay >= 48 && ay * 2 >= ax * 3)) {
        *input = (JelliInput){JELLI_SWIPE, ax > ay ? dx : 0, ay > ax ? dy : 0};
        return true;
    }
    return false;
}

bool jelli_gesture_end_at(JelliGesture *gesture, int x, int y, uint64_t now_ms, JelliInput *input)
{
    bool active = gesture->active;
    (void)jelli_gesture_move(gesture, x, y);
    gesture->active = false;
    if (!active || now_ms < gesture->started_ms || x < 0 || y < 0 || x >= JELLI_WIDTH ||
        y >= JELLI_HEIGHT)
        return false;
    int dx = x - gesture->x, dy = y - gesture->y;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax <= 18 && ay <= 18) {
        if (gesture->moved)
            return false;
        bool long_press = now_ms - gesture->started_ms >= JELLI_HOLD_MS;
        *input = (JelliInput){long_press ? JELLI_LONG_PRESS : JELLI_TAP,
                              long_press ? x : gesture->x, long_press ? y : gesture->y};
        return true;
    }
    return swipe_input(dx, dy, ax, ay, input);
}

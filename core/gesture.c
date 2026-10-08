#include "jelli/gesture.h"

void jelli_gesture_begin(JelliGesture *gesture, int x, int y)
{
    *gesture = (JelliGesture){x, y, x >= 0 && y >= 0 && x < JELLI_WIDTH && y < JELLI_HEIGHT};
}

bool jelli_gesture_end(JelliGesture *gesture, int x, int y, JelliInput *input)
{
    bool active = gesture->active;
    gesture->active = false;
    if (!active || x < 0 || y < 0 || x >= JELLI_WIDTH || y >= JELLI_HEIGHT)
        return false;
    int dx = x - gesture->x, dy = y - gesture->y;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax <= 18 && ay <= 18) {
        *input = (JelliInput){JELLI_TAP, gesture->x, gesture->y};
        return true;
    }
    /* Dead band and 3:2 dominance reject ambiguous drags and diagonals. */
    if ((ax >= 48 && ax * 2 >= ay * 3) || (ay >= 48 && ay * 2 >= ax * 3)) {
        *input = (JelliInput){JELLI_SWIPE, ax > ay ? dx : 0, ay > ax ? dy : 0};
        return true;
    }
    return false;
}

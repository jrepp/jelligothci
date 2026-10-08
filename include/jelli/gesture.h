#ifndef JELLI_GESTURE_H
#define JELLI_GESTURE_H
#include "jelli/engine.h"
/* Host-owned; confined to its input task. No timers or allocation. */
typedef struct {
    int x, y;
    bool active;
} JelliGesture;
void jelli_gesture_begin(JelliGesture *gesture, int x, int y);
bool jelli_gesture_end(JelliGesture *gesture, int x, int y, JelliInput *input);
#endif

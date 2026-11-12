#ifndef JELLI_GESTURE_H
#define JELLI_GESTURE_H
#include "jelli/engine.h"
#define JELLI_HOLD_MS 3000u
/* Host-owned; confined to its input task. No timers or allocation. */
typedef struct {
    int x, y, last_x, last_y;
    bool active, moved;
    uint64_t started_ms;
} JelliGesture;
bool jelli_gesture_move(JelliGesture *gesture, int x, int y);
void jelli_gesture_begin_at(JelliGesture *gesture, int x, int y, uint64_t now_ms);
bool jelli_gesture_end_at(JelliGesture *gesture, int x, int y, uint64_t now_ms, JelliInput *input);
void jelli_gesture_begin(JelliGesture *gesture, int x, int y);
bool jelli_gesture_end(JelliGesture *gesture, int x, int y, JelliInput *input);
#endif

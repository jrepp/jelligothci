#include "jelli/gesture.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    JelliGesture gesture = {0};
    JelliInput input = {JELLI_QUIT, 0, 0};
    CHECK(!jelli_gesture_end(&gesture, 200, 200, &input));
    jelli_gesture_begin(&gesture, 200, 200);
    CHECK(jelli_gesture_end(&gesture, 218, 182, &input));
    CHECK(input.kind == JELLI_TAP && input.x == 200 && input.y == 200);
    CHECK(!jelli_gesture_end(&gesture, 218, 182, &input));
    jelli_gesture_begin(&gesture, 200, 200);
    CHECK(jelli_gesture_end(&gesture, 152, 220, &input));
    CHECK(input.kind == JELLI_SWIPE && input.x == -48 && input.y == 0);
    jelli_gesture_begin(&gesture, 200, 200);
    CHECK(jelli_gesture_end(&gesture, 200, 80, &input));
    CHECK(input.kind == JELLI_SWIPE && input.y == -120);
    jelli_gesture_begin(&gesture, 200, 200);
    CHECK(!jelli_gesture_end(&gesture, 230, 200, &input));
    jelli_gesture_begin(&gesture, 200, 200);
    CHECK(!jelli_gesture_end(&gesture, 270, 270, &input));
    jelli_gesture_begin(&gesture, -1, 200);
    CHECK(!jelli_gesture_end(&gesture, 100, 200, &input));
    jelli_gesture_begin(&gesture, 200, 200);
    CHECK(!jelli_gesture_end(&gesture, 466, 200, &input));
    CHECK(!gesture.active);
    puts("Gesture thresholds, tap exclusivity, diagonals and bounds verified.");
    return 0;
}

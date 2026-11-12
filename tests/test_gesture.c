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
    jelli_gesture_begin_at(&gesture, 200, 200, 100u);
    CHECK(jelli_gesture_end_at(&gesture, 200, 200, 3099u, &input) && input.kind == JELLI_TAP);
    jelli_gesture_begin_at(&gesture, 200, 200, 100u);
    CHECK(jelli_gesture_end_at(&gesture, 200, 200, 3100u, &input) &&
          input.kind == JELLI_LONG_PRESS);
    jelli_gesture_begin_at(&gesture, 200, 200, 100u);
    CHECK(jelli_gesture_move(&gesture, 219, 200));
    CHECK(!jelli_gesture_end_at(&gesture, 200, 200, 4000u, &input));
    jelli_gesture_begin_at(&gesture, 200, 200, 5000u);
    CHECK(!jelli_gesture_end_at(&gesture, 200, 200, 100u, &input));
    puts("Gesture thresholds, tap exclusivity, diagonals and bounds verified.");
    return 0;
}

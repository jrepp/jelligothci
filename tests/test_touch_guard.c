#include "jelli/touch_guard.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void canceled(void *ctx) { ++*(unsigned *)ctx; }

int main(void)
{
    JelliGesture gesture = {0};
    unsigned cancellations = 0u;
    JelliTouchGuard guard = {.gesture = &gesture, .cancel = canceled, .cancel_ctx = &cancellations};
    JelliInput input;
    uint8_t contacts = 1;
    jelli_gesture_begin(&gesture, 200, 200);
    jelli_touch_guard_apply(&guard, true, &contacts);
    CHECK(!contacts && guard.awaiting_release && cancellations == 1u);
    CHECK(!jelli_gesture_end(&gesture, 200, 200, &input));
    contacts = 1;
    jelli_touch_guard_apply(&guard, false, &contacts);
    CHECK(!contacts && guard.awaiting_release);
    /* Even a failed empty report cannot rearm. */
    jelli_touch_guard_apply(&guard, true, &contacts);
    CHECK(guard.awaiting_release && cancellations == 1u);
    jelli_touch_guard_apply(&guard, false, &contacts);
    CHECK(!guard.awaiting_release);
    contacts = 1;
    jelli_touch_guard_apply(&guard, false, &contacts);
    CHECK(contacts == 1);
    jelli_gesture_begin(&gesture, 200, 200);
    CHECK(jelli_gesture_end(&gesture, 200, 200, &input));
    CHECK(input.kind == JELLI_TAP);
    return 0;
}

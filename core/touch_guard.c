#include "jelli/touch_guard.h"

void jelli_touch_guard_apply(JelliTouchGuard *guard, bool failed, uint8_t *count)
{
    if (failed) {
        guard->awaiting_release = true;
        guard->gesture->active = false;
        *count = 0;
    } else if (guard->awaiting_release) {
        if (*count == 0)
            guard->awaiting_release = false;
        *count = 0;
    }
}

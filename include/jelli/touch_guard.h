#ifndef JELLI_TOUCH_GUARD_H
#define JELLI_TOUCH_GUARD_H
#include "jelli/gesture.h"

typedef struct {
    JelliGesture *gesture;
    bool awaiting_release;
    void (*cancel)(void *ctx);
    void *cancel_ctx;
} JelliTouchGuard;
/* Caller owns guard and gesture, serializes access, and supplies valid pointers.
 * Call before translating a sample to gesture events. Errors cancel the gesture;
 * a valid zero-contact sample is required before accepting the next contact.
 * The optional cancel callback runs once when an active contact fails, in the
 * caller's context, before the gesture is cleared. */
void jelli_touch_guard_apply(JelliTouchGuard *guard, bool failed, uint8_t *count);
#endif

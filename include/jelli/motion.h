#ifndef JELLI_MOTION_H
#define JELLI_MOTION_H
#include "jelli/device.h"
/* One coalesced event, caller-owned and serialized. A worker/ISR must use host
 * synchronization before publishing; volatile does not make this thread safe.
 * Can be supplied by any sensor or the desktop simulator. No allocations. */
typedef struct {
    JelliMotionSample pending;
    JelliDeviceResult result;
} JelliMotionMailbox;
_Static_assert(sizeof(JelliMotionMailbox) <= 32u, "Motion mailbox budget");
void jelli_motion_publish(JelliMotionMailbox *mailbox, JelliDeviceResult result,
                          JelliMotionSample sample);
JelliDeviceResult jelli_motion_poll(void *ctx, JelliMotionSample *sample);
#endif

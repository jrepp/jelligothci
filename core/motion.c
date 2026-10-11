#include "jelli/motion.h"

void jelli_motion_publish(JelliMotionMailbox *mailbox, JelliDeviceResult result,
                          JelliMotionSample sample)
{
    mailbox->result = result;
    if (result != JELLI_DEVICE_OK)
        mailbox->pending = (JelliMotionSample){0};
    else if (sample.detected)
        mailbox->pending = sample;
}

JelliDeviceResult jelli_motion_poll(void *ctx, JelliMotionSample *sample)
{
    if (!ctx || !sample)
        return JELLI_DEVICE_INVALID;
    JelliMotionMailbox *mailbox = ctx;
    if (mailbox->result == JELLI_DEVICE_OK) {
        *sample = mailbox->pending;
        mailbox->pending = (JelliMotionSample){0};
    }
    return mailbox->result;
}

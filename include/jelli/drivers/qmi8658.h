#ifndef JELLI_QMI8658_H
#define JELLI_QMI8658_H
#include "jelli/drivers/register_io.h"

enum { JELLI_QMI_REG_COUNT = 10 };
typedef struct {
    uint8_t before[JELLI_QMI_REG_COUNT], after[JELLI_QMI_REG_COUNT], armed[JELLI_QMI_REG_COUNT];
    uint8_t who, revision, firmware[3], usid[6], status, ack_status;
    uint32_t commands;
    JelliDeviceResult command_error, ack_error;
    bool saved, changed, wom_attempted, status_valid, restored;
} JelliQmi8658;
/* Explicit qualification protocol for WHO=05/revision=7c. The caller must
 * provide exclusive sensor ownership, including absence of prior hidden WoM.
 * No suffix is inferred from identity. int2 is an explicit board-qualified
 * opt-in, restricted to observed firmware 1e0301; it is NOT wake readiness.
 * begin always initializes state. Call restore after every begin attempt before
 * reusing state; failed restore requires host fault handling, not a new begin.
 * Setup/cleanup may wait: host worker/validation context only, never a frame.
 * Register reads deliberately do not assume auto-increment. */
JelliDeviceResult jelli_qmi8658_begin(JelliRegisterIo io, JelliQmi8658 *sensor, bool motion,
                                      bool enable_int2);
JelliDeviceResult jelli_qmi8658_ack_motion(JelliRegisterIo io, JelliQmi8658 *sensor);
JelliDeviceResult jelli_qmi8658_restore(JelliRegisterIo io, JelliQmi8658 *sensor);
#endif

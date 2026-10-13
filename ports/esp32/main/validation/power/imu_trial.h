#ifndef JELLI_IMU_TRIAL_H
#define JELLI_IMU_TRIAL_H
#include "jelli/debug.h"
#include "display_output.h"
void jelli_imu_trial_init(void);
bool jelli_imu_trial_busy(void);
bool jelli_imu_trial_command(JelliDebug *debug, uint32_t id, char **words, unsigned count);
void jelli_imu_trial_poll(JelliDisplayOutput *output, JelliPetEngine *engine);
#endif

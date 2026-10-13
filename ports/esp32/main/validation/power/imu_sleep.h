#ifndef JELLI_IMU_SLEEP_H
#define JELLI_IMU_SLEEP_H
#include "display_output.h"
/* Fixed engine-task state; reset once before every IMU diagnostic. */
void jelli_imu_sleep_reset(void);
esp_err_t jelli_imu_sleep_run(JelliDisplayOutput *output, JelliPetEngine *engine);
bool jelli_imu_sleep_completed(void);
void jelli_imu_sleep_report(char *report, size_t capacity);
#endif

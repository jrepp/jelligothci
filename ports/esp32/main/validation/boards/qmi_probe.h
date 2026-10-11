#ifndef JELLI_QMI_PROBE_H
#define JELLI_QMI_PROBE_H
#include "driver/i2c_master.h"
#include <stdbool.h>
#include <stdint.h>

enum { JELLI_QMI_REG_COUNT = 10 };
typedef struct {
    uint8_t before[JELLI_QMI_REG_COUNT], after[JELLI_QMI_REG_COUNT];
    uint8_t who, revision, firmware[3], usid[6], status, ack_status;
    uint32_t commands;
    bool saved, changed, wom_attempted, status_valid, restored;
} JelliQmiProbe;
/* One startup I2C handle; all operations owned by the engine/validation task.
 * Only disabled-sensor reset controls are supported; no arbitrary prior WoM. */
esp_err_t jelli_qmi_open(i2c_master_bus_handle_t bus);
esp_err_t jelli_qmi_begin(JelliQmiProbe *probe, bool motion);
esp_err_t jelli_qmi_ack_motion(JelliQmiProbe *probe);
esp_err_t jelli_qmi_restore(JelliQmiProbe *probe);
#endif

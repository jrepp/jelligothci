#ifndef JELLI_QMI_PROBE_H
#define JELLI_QMI_PROBE_H
#include "driver/i2c_master.h"
#include "jelli/drivers/qmi8658.h"

typedef JelliQmi8658 JelliQmiProbe;
/* Validation owns a single startup handle. Normal firmware does not arm WoM. */
esp_err_t jelli_qmi_open(i2c_master_bus_handle_t bus);
esp_err_t jelli_qmi_begin(JelliQmiProbe *probe, bool motion, bool enable_int2);
esp_err_t jelli_qmi_ack_motion(JelliQmiProbe *probe);
esp_err_t jelli_qmi_restore(JelliQmiProbe *probe);
#endif

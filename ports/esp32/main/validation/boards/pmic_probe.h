#ifndef JELLI_PMIC_PROBE_H
#define JELLI_PMIC_PROBE_H
#include "driver/i2c_master.h"
#include <stdint.h>

/* SKU 31261 AXP2101. Caller owns handle; attach once after BSP bus initialization.
 * Each read has a 50 ms deadline. No rail writes or status-clearing reads. */
esp_err_t jelli_pmic_probe_open(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *device);
esp_err_t jelli_pmic_probe_read(i2c_master_dev_handle_t device, uint8_t values[4]);
#endif

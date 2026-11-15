#ifndef JELLI_BATTERY_MONITOR_H
#define JELLI_BATTERY_MONITOR_H
#include "driver/i2c_master.h"
#include "jelli/pet_engine.h"
#include <stdbool.h>

/* Startup creates a single worker and bounded latest-value queues. Call after
 * NVS initialization. The worker owns PMIC I2C and power-history storage. */
bool jelli_battery_monitor_start(i2c_master_bus_handle_t bus);
void jelli_battery_monitor_poll(JelliPetEngine *engine, bool display_blank, uint64_t now);
#endif

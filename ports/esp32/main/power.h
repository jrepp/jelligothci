#ifndef JELLI_ESP_POWER_H
#define JELLI_ESP_POWER_H

#include "jelli/pet_engine.h"
#include "driver/i2c_master.h"

#define JELLI_ESP_POWER_INTERVAL_MS 10000u

typedef struct {
    i2c_master_dev_handle_t pmu;
    uint64_t last_ms;
    bool sampled, failing;
} JelliEspPower;

/* Adds the AXP2101 (7-bit 0x34) to the BSP I2C bus. Read-only: never writes a PMU register. */
void jelli_esp_power_open(JelliEspPower *power);
/* Engine task, outside the display lock. Samples at most once per interval; any I2C
 * failure reports an unknown reading and logs once until a read succeeds again. */
void jelli_esp_power_poll(JelliEspPower *power, JelliPetEngine *engine, uint64_t now);

#endif

#include "power.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

/* AXP2101 register map, verified against X-Powers' AXP2101 datasheet V1.0
 * (section 6.13.2) and Waveshare's XPowersLib copy (src/REG/AXP2101Constants.h,
 * src/XPowersAXP2101.tpp), as used by Waveshare's ESP-IDF 01_AXP2101 example:
 *   0x00 PMU status 1: bit 5 VBUS good, bit 3 battery present.
 *   0x01 PMU status 2: bits 6:5 current direction (00 standby, 01 charge,
 *        10 discharge); bits 2:0 charger state (100 charge done).
 *   0x18 bit 3: fuel gauge enabled (power-on default 1).
 *   0xA4: battery percentage, 0..100, valid when the gauge is enabled.
 * Read-only by design: charger, ADC, gauge, and power-path configuration need
 * separate authorization, so a disabled gauge reports an unknown percentage. */
enum {
    PMU_ADDRESS = 0x34,
    REG_STATUS1 = 0x00,
    REG_STATUS2 = 0x01,
    REG_GAUGE_CONTROL = 0x18,
    REG_PERCENT = 0xa4,
    STATUS1_VBUS_GOOD = 1 << 5,
    STATUS1_BATTERY_PRESENT = 1 << 3,
    GAUGE_ENABLED = 1 << 3,
    DIRECTION_CHARGE = 1,
    DIRECTION_DISCHARGE = 2,
    CHARGER_DONE = 4,
    I2C_TIMEOUT_MS = 10
};

static const char *TAG = "jelli_power";

void jelli_esp_power_open(JelliEspPower *power)
{
    *power = (JelliEspPower){0};
    i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                  .device_address = PMU_ADDRESS,
                                  .scl_speed_hz = 100000u};
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus || i2c_master_bus_add_device(bus, &config, &power->pmu) != ESP_OK) {
        power->pmu = NULL;
        ESP_LOGW(TAG, "AXP2101 unavailable on the BSP I2C bus; battery state unknown");
    }
}

static bool read_register(const JelliEspPower *power, uint8_t reg, uint8_t *value)
{
    return power->pmu &&
           i2c_master_transmit_receive(power->pmu, &reg, 1u, value, 1u, I2C_TIMEOUT_MS) == ESP_OK;
}

static JelliPowerReading decode(uint8_t status1, uint8_t status2, bool gauge, uint8_t percent)
{
    JelliPowerReading reading = {JELLI_POWER_UNKNOWN, JELLI_POWER_PERCENT_UNKNOWN};
    bool vbus = (status1 & STATUS1_VBUS_GOOD) != 0u;
    if (!(status1 & STATUS1_BATTERY_PRESENT)) {
        reading.state = vbus ? JELLI_POWER_NO_BATTERY : JELLI_POWER_UNKNOWN;
        return reading;
    }
    unsigned direction = (status2 >> 5) & 3u;
    if (vbus && (status2 & 7u) == CHARGER_DONE) /* "Done" only means full while plugged in. */
        reading.state = JELLI_POWER_CHARGED;
    else if (direction == DIRECTION_CHARGE)
        reading.state = JELLI_POWER_CHARGING;
    else /* DIRECTION_DISCHARGE, or plugged in but idle: show the stored level. */
        reading.state = JELLI_POWER_DISCHARGING;
    if (gauge && percent <= 100u)
        reading.percent = percent;
    return reading;
}

void jelli_esp_power_poll(JelliEspPower *power, JelliPetEngine *engine, uint64_t now)
{
    if (power->sampled && now >= power->last_ms &&
        now - power->last_ms < JELLI_ESP_POWER_INTERVAL_MS)
        return;
    power->sampled = true;
    power->last_ms = now;
    uint8_t status1 = 0u, status2 = 0u, control = 0u, percent = 0u;
    bool ok = read_register(power, REG_STATUS1, &status1) &&
              read_register(power, REG_STATUS2, &status2) &&
              read_register(power, REG_GAUGE_CONTROL, &control);
    bool gauge = ok && (control & GAUGE_ENABLED) && read_register(power, REG_PERCENT, &percent);
    if (!ok) {
        engine->ui.power = (JelliPowerReading){JELLI_POWER_UNKNOWN, JELLI_POWER_PERCENT_UNKNOWN};
        if (power->pmu && !power->failing)
            ESP_LOGW(TAG, "AXP2101 read failed; battery state unknown until it recovers");
        power->failing = true;
        return;
    }
    if (power->failing)
        ESP_LOGI(TAG, "AXP2101 readings recovered");
    power->failing = false;
    engine->ui.power = decode(status1, status2, gauge, percent);
}

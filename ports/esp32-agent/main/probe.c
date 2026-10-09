#include "probe.h"
#include "jelli/agent_pmu.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/pulse_cnt.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

enum { PMU_ADDRESS = 0x34, I2C_TIMEOUT_MS = 10, PWR_SAMPLE_MS = 2 };

static void error(char *out, size_t capacity, const char *code)
{
    (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"%s\"}", code);
}

/* Build metadata goes into JSON strings: keep only characters that need no escaping. */
static void plain(char *out, size_t capacity, const char *text, size_t limit)
{
    size_t used = 0u;
    for (size_t i = 0u; i < limit && text[i] && used + 1u < capacity; ++i) {
        char c = text[i];
        if (c >= ' ' && c <= '~' && c != '"' && c != '\\')
            out[used++] = c;
    }
    out[used] = '\0';
}

static const char *image_state(const esp_partition_t *running)
{
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK)
        return "none";
    switch (state) {
    case ESP_OTA_IMG_NEW:
        return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "pending_verify";
    case ESP_OTA_IMG_VALID:
        return "valid";
    case ESP_OTA_IMG_INVALID:
        return "invalid";
    case ESP_OTA_IMG_ABORTED:
        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
    default:
        return "undefined";
    }
}

void jelli_agent_probe_chip(char *out, size_t capacity)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t jedec = 0u, flash = 0u;
    if (esp_flash_read_id(NULL, &jedec) != ESP_OK)
        jedec = 0u;
    if (esp_flash_get_physical_size(NULL, &flash) != ESP_OK)
        flash = 0u;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app = esp_app_get_description();
    char label[17], version[33], idf[33];
    if (running)
        plain(label, sizeof(label), running->label, sizeof(running->label));
    else
        plain(label, sizeof(label), "?", 1u);
    plain(version, sizeof(version), app->version, sizeof(app->version));
    plain(idf, sizeof(idf), app->idf_ver, sizeof(app->idf_ver));
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"model\":%d,\"revision\":%u,\"cores\":%u,\"cpu_mhz\":%d,"
                   "\"jedec\":\"%06" PRIx32 "\",\"flash_bytes\":%" PRIu32
                   ",\"psram_bytes\":%u,\"partition\":\"%s\",\"image_state\":\"%s\","
                   "\"version\":\"%s\",\"idf\":\"%s\"}",
                   (int)chip.model, (unsigned)chip.revision, (unsigned)chip.cores,
                   CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, jedec, flash, (unsigned)esp_psram_get_size(),
                   label, image_state(running), version, idf);
}

void jelli_agent_probe_heap(char *out, size_t capacity)
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"internal_free\":%u,\"internal_min\":%u,"
                   "\"internal_largest\":%u,\"dma_free\":%u,\"psram_free\":%u,"
                   "\"psram_largest\":%u}",
                   (unsigned)heap_caps_get_free_size(internal),
                   (unsigned)heap_caps_get_minimum_free_size(internal),
                   (unsigned)heap_caps_get_largest_free_block(internal),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

/* Each read writes only the register address, then reads one byte; nothing in the
 * PMU changes. The device is added and removed per probe so it holds no bus slot. */
static bool read_pmu(uint8_t values[4])
{
    static const uint8_t registers[4] = {JELLI_AGENT_PMU_POWER_OFF_ENABLES,
                                         JELLI_AGENT_PMU_KEY_LEVELS, JELLI_AGENT_PMU_LDO_ENABLES,
                                         JELLI_AGENT_PMU_ALDO1_VOLTAGE};
    i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                  .device_address = PMU_ADDRESS,
                                  .scl_speed_hz = 100000u};
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    i2c_master_dev_handle_t pmu = NULL;
    bool ok = bus && i2c_master_bus_add_device(bus, &config, &pmu) == ESP_OK;
    for (unsigned i = 0u; ok && i < 4u; ++i)
        ok = i2c_master_transmit_receive(pmu, &registers[i], 1u, &values[i], 1u, I2C_TIMEOUT_MS) ==
             ESP_OK;
    if (pmu)
        (void)i2c_master_bus_rm_device(pmu);
    return ok;
}

void jelli_agent_probe_pmu(char *out, size_t capacity)
{
    uint8_t raw[4] = {0u, 0u, 0u, 0u};
    if (!read_pmu(raw)) {
        error(out, capacity, "pmu_unavailable");
        return;
    }
    JelliAgentPmu pmu = jelli_agent_pmu_decode(raw[0], raw[1], raw[2], raw[3]);
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"raw\":{\"0x22\":%u,\"0x27\":%u,\"0x90\":%u,\"0x92\":%u},"
                   "\"aldo1_on\":%s,\"aldo1_mv\":%u,\"key_on_ms\":%u,\"key_off_ms\":%u,"
                   "\"key_irq_ms\":%u,\"long_press_off\":%s,\"long_press_restarts\":%s,"
                   "\"over_temperature_off\":%s}",
                   raw[0], raw[1], raw[2], raw[3], pmu.aldo1_on ? "true" : "false", pmu.aldo1_mv,
                   pmu.on_ms, pmu.off_ms, pmu.irq_ms, pmu.long_press_off ? "true" : "false",
                   pmu.long_press_restarts ? "true" : "false",
                   pmu.over_temperature_off ? "true" : "false");
}

void jelli_agent_probe_pwr(char *out, size_t capacity, uint32_t ms, unsigned pull)
{
#ifdef JELLI_BOARD_PWR_KEY_GPIO
    /* No pull-down: if the BSS138 shifter is bidirectional, pulling GPIO3 low could
     * read as a press of PWRON, and a 6 s press powers the board off. */
    if (ms < PWR_SAMPLE_MS || ms > 10000u || pull > 1u) {
        error(out, capacity, "range");
        return;
    }
    const gpio_num_t pin = (gpio_num_t)JELLI_BOARD_PWR_KEY_GPIO;
    /* Input only: a pull resistor of tens of kilohms never drives the line. */
    gpio_config_t input = {.pin_bit_mask = 1ull << JELLI_BOARD_PWR_KEY_GPIO,
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = pull ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
    if (gpio_config(&input) != ESP_OK) {
        error(out, capacity, "gpio");
        return;
    }
    int first = gpio_get_level(pin), last = first;
    unsigned samples = ms / PWR_SAMPLE_MS, low = 0u, changes = 0u;
    for (unsigned i = 0u; i < samples; ++i) {
        int level = gpio_get_level(pin);
        low += level == 0 ? 1u : 0u;
        changes += level != last ? 1u : 0u;
        last = level;
        vTaskDelay(pdMS_TO_TICKS(PWR_SAMPLE_MS));
    }
    (void)gpio_set_pull_mode(pin, GPIO_FLOATING); /* As it was before the probe. */
    static const char *const pulls[] = {"none", "up"};
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"gpio\":%d,\"pull\":\"%s\",\"first\":%d,\"last\":%d,"
                   "\"samples\":%u,\"low\":%u,\"changes\":%u}",
                   JELLI_BOARD_PWR_KEY_GPIO, pulls[pull], first, last, samples, low, changes);
#else
    (void)ms;
    (void)pull;
    error(out, capacity, "unsupported_board");
#endif
}

#ifdef JELLI_BOARD_LCD_TE_GPIO
/* Counts rising edges in hardware; the handles are released on every path. */
static bool count_edges(uint32_t ms, int *count)
{
    pcnt_unit_config_t unit_config = {.low_limit = -1, .high_limit = 32767};
    pcnt_chan_config_t channel_config = {.edge_gpio_num = JELLI_BOARD_LCD_TE_GPIO,
                                         .level_gpio_num = -1};
    pcnt_unit_handle_t unit = NULL;
    pcnt_channel_handle_t channel = NULL;
    bool ok = pcnt_new_unit(&unit_config, &unit) == ESP_OK &&
              pcnt_new_channel(unit, &channel_config, &channel) == ESP_OK &&
              pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                           PCNT_CHANNEL_EDGE_ACTION_HOLD) == ESP_OK;
    bool enabled = ok && pcnt_unit_enable(unit) == ESP_OK;
    ok = enabled && pcnt_unit_clear_count(unit) == ESP_OK && pcnt_unit_start(unit) == ESP_OK;
    if (ok) {
        vTaskDelay(pdMS_TO_TICKS(ms));
        ok = pcnt_unit_stop(unit) == ESP_OK && pcnt_unit_get_count(unit, count) == ESP_OK;
    }
    if (enabled)
        (void)pcnt_unit_disable(unit);
    if (channel)
        (void)pcnt_del_channel(channel);
    if (unit)
        (void)pcnt_del_unit(unit);
    return ok;
}
#endif

void jelli_agent_probe_te(char *out, size_t capacity, uint32_t ms)
{
#ifdef JELLI_BOARD_LCD_TE_GPIO
    if (ms < 100u || ms > 5000u) {
        error(out, capacity, "range");
        return;
    }
    int count = 0;
    if (!count_edges(ms, &count) || count < 0) {
        error(out, capacity, "pcnt");
        return;
    }
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"gpio\":%d,\"ms\":%" PRIu32 ",\"edges\":%d,"
                   "\"hz_x100\":%" PRIu32 "}",
                   JELLI_BOARD_LCD_TE_GPIO, ms, count, (uint32_t)count * 100000u / ms);
#else
    (void)ms;
    error(out, capacity, "unsupported_board");
#endif
}

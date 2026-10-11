#include "suite.h"
#include "../boards/pmic_probe.h"
#include "../boards/inventory.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static uint8_t *scratch;
static i2c_master_dev_handle_t pmic;
static esp_err_t board_error, identity_error;
static JelliFactoryState state = {.phase = "idle", .scope = "none"};
static JelliFactoryTest tests[JELLI_FACTORY_TEST_COUNT] = {
    {.id = "identity", .status = "not_run", .supported = true},
    {.id = "psram.scratch", .status = "not_run", .supported = true},
    {.id = "pmic.read", .status = "not_run", .supported = true},
    {.id = "display.visible", .status = "not_run"},
    {.id = "touch.physical", .status = "not_run"},
    {.id = "audio.audible", .status = "not_run"},
    {.id = "power.current", .status = "not_run"},
    {.id = "pmic.inventory", .status = "not_run", .supported = true},
    {.id = "imu.identity", .status = "not_run", .supported = true},
    {.id = "rtc.snapshot", .status = "not_run", .supported = true}};

void jelli_factory_init(void)
{
    state.boot = esp_random();
    uint8_t mac[6] = {0};
    identity_error = esp_efuse_mac_get_default(mac);
    (void)snprintf(state.device, sizeof(state.device), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1],
                   mac[2], mac[3], mac[4], mac[5]);
    const esp_app_desc_t *app = esp_app_get_description();
    for (unsigned i = 0; i < 32; ++i)
        (void)snprintf(&state.elf_sha256[i * 2], 3, "%02x", app->app_elf_sha256[i]);
    scratch = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    board_error = bsp_i2c_init();
    jelli_inventory_init(bsp_i2c_get_handle(), board_error);
    if (board_error == ESP_OK)
        board_error = jelli_pmic_probe_open(bsp_i2c_get_handle(), &pmic);
}

const JelliFactoryState *jelli_factory_state(void) { return &state; }
const JelliFactoryTest *jelli_factory_test(unsigned index)
{
    return index < JELLI_FACTORY_TEST_COUNT ? &tests[index] : NULL;
}

esp_err_t jelli_factory_begin(const char *scope)
{
    if (!strcmp(state.phase, "running"))
        return ESP_ERR_INVALID_STATE;
    unsigned selected = JELLI_FACTORY_TEST_COUNT;
    bool all = !strcmp(scope, "all");
    for (unsigned i = 0; i < JELLI_FACTORY_TEST_COUNT; ++i)
        if (!strcmp(tests[i].id, scope))
            selected = i;
    if (!all && selected == JELLI_FACTORY_TEST_COUNT)
        return ESP_ERR_NOT_FOUND;
    if (++state.run == 0)
        ++state.run;
    state.phase = "running";
    state.scope = all ? "all" : tests[selected].id;
    jelli_inventory_clear();
    memset(state.pmic_values, 0, sizeof(state.pmic_values));
    for (unsigned i = 0; i < JELLI_FACTORY_TEST_COUNT; ++i) {
        tests[i].status = all || i == selected ? "pending" : "not_run";
        tests[i].error = ESP_OK;
        tests[i].elapsed_us = 0;
    }
    return ESP_OK;
}

static esp_err_t memory_test(void)
{
    if (!scratch)
        return ESP_ERR_NO_MEM;
    const uint8_t patterns[] = {0x55, 0xaa};
    for (unsigned p = 0; p < sizeof(patterns); ++p) {
        memset(scratch, patterns[p], 1024);
        volatile const uint8_t *observed = scratch;
        for (unsigned i = 0; i < 1024; ++i)
            if (observed[i] != patterns[p])
                return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t execute(unsigned index)
{
    if (index == 0)
        return identity_error;
    if (index == 1)
        return memory_test();
    if (index == 2)
        return board_error == ESP_OK ? jelli_pmic_probe_read(pmic, state.pmic_values) : board_error;
    if (index >= 7 && index < JELLI_FACTORY_TEST_COUNT)
        return jelli_inventory_read(index - 7);
    return ESP_ERR_NOT_SUPPORTED;
}

void jelli_factory_poll(void)
{
    if (strcmp(state.phase, "running"))
        return;
    for (unsigned i = 0; i < JELLI_FACTORY_TEST_COUNT; ++i) {
        if (strcmp(tests[i].status, "pending"))
            continue;
        int64_t start = esp_timer_get_time();
        tests[i].error = execute(i);
        tests[i].elapsed_us = esp_timer_get_time() - start;
        tests[i].status = !tests[i].supported        ? "skip"
                          : tests[i].error == ESP_OK ? "pass"
                                                     : "error";
        return;
    }
    state.phase = "incomplete";
    for (unsigned i = 0; i < JELLI_FACTORY_TEST_COUNT; ++i)
        if (!strcmp(tests[i].status, "error"))
            state.phase = "error";
}

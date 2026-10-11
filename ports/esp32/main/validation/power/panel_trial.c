#include "panel_trial.h"
#include "touch_trial.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_lcd_co5300.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

enum { NONE, BASELINE, DIM30, DIM10, BLACK, OFF, SLEEP, STANDBY };
static const char *const names[] = {"none",  "baseline", "dim30", "dim10",
                                    "black", "off",      "sleep", "standby"};
static unsigned pending;
static uint32_t run, boot;
static bool deep_standby = true, faulted;
static char report[768] = "{\"ok\":true,\"status\":\"idle\"}";

/* Only the paused, engine-task-owned trial changes this policy; all driver
 * sleep state remains in the vendor driver. Other power trials default deep. */
bool jelli_panel_trial_deep_standby(void) { return deep_standby; }
bool jelli_panel_trial_busy(void) { return pending != NONE; }

bool jelli_panel_trial_command(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    if (count < 4 || strcmp(words[2], "power") || strcmp(words[3], "panel"))
        return false;
    if (count == 5 && !strcmp(words[4], "result")) {
        jelli_debug_response(debug, id, report);
        return true;
    }
    if (pending || faulted) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy_or_faulted\"}");
        return true;
    }
    if (count == 6 && !strcmp(words[4], "run") && !debug->captured && !jelli_touch_trial_busy()) {
        for (unsigned i = BASELINE; i <= STANDBY; ++i)
            if (!strcmp(words[5], names[i]))
                pending = i;
    }
    if (!pending) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"mode_or_busy\"}");
        return true;
    }
    if (!boot)
        boot = esp_random();
    run = run == UINT32_MAX ? 1u : run + 1u;
    char accepted[112];
    (void)snprintf(accepted, sizeof(accepted),
                   "{\"ok\":true,\"pending\":true,\"run\":%lu,\"boot\":%lu}", (unsigned long)run,
                   (unsigned long)boot);
    jelli_debug_response(debug, id, accepted);
    return true;
}

static esp_err_t restore(esp_lcd_panel_handle_t panel, unsigned mode, int brightness)
{
    esp_err_t result = ESP_OK;
    if (mode == OFF)
        result = esp_lcd_panel_disp_on_off(panel, true);
    if (mode == SLEEP || mode == STANDBY)
        result = esp_lcd_panel_disp_sleep(panel, false);
    esp_err_t light = esp_lcd_panel_co5300_set_brightness(panel, (uint8_t)brightness);
    return result == ESP_OK ? light : result;
}

static esp_err_t enter(esp_lcd_panel_handle_t panel, unsigned mode)
{
    if (mode == OFF)
        return esp_lcd_panel_disp_on_off(panel, false);
    if (mode == SLEEP || mode == STANDBY)
        return esp_lcd_panel_disp_sleep(panel, true);
    return ESP_OK;
}

static void restored_brightness(JelliDisplayOutput *output, esp_err_t result, int before)
{
    output->brightness_known = result == ESP_OK && before >= 0;
    if (output->brightness_known)
        output->brightness_percent = (unsigned)before;
}

static void execute(JelliDisplayOutput *output, unsigned mode)
{
    int before = output->brightness_known ? (int)output->brightness_percent : -1;
    int target = mode == DIM30 ? 30 : mode == DIM10 ? 10 : mode == BLACK ? 0 : before;
    esp_lcd_panel_handle_t panel = output->transfer.panel;
    esp_err_t error = !panel || before < 0 || before > 100 ? ESP_ERR_INVALID_STATE : ESP_OK;
    bool paused = false, entered = false;
    if (error == ESP_OK) {
        error = esp_lv_adapter_pause(2000);
        paused = error == ESP_OK;
    }
    int64_t start = esp_timer_get_time();
    if (error == ESP_OK) {
        /* Blocking checked command drains preceding SPI color transactions. */
        error = esp_lcd_panel_co5300_set_brightness(panel, (uint8_t)target);
    }
    deep_standby = mode != SLEEP;
    if (error == ESP_OK) {
        entered = true;
        error = enter(panel, mode);
    }
    int64_t entry_us = esp_timer_get_time() - start;
    start = esp_timer_get_time();
    if (error == ESP_OK)
        vTaskDelay(pdMS_TO_TICKS(200));
    int64_t hold_us = esp_timer_get_time() - start;
    start = esp_timer_get_time();
    esp_err_t restored = paused ? restore(panel, entered ? mode : NONE, before) : ESP_OK;
    deep_standby = true;
    int64_t recovery_us = esp_timer_get_time() - start;
    if (paused && esp_lv_adapter_resume() != ESP_OK)
        restored = ESP_FAIL;
    restored_brightness(output, restored, before);
    faulted = error != ESP_OK || restored != ESP_OK;
    output->refresh_requested = true;
    int size =
        snprintf(report, sizeof(report),
                 "{\"ok\":true,\"status\":\"complete\",\"run\":%lu,\"boot\":%lu,\"mode\":\"%s\","
                 "\"error\":%d,"
                 "\"restore_error\":%d,\"brightness_before\":%d,\"brightness_requested\":%d,"
                 "\"brightness_restore_requested\":%d,\"entry_us\":%lld,\"hold_us\":%lld,"
                 "\"recovery_us\":%lld,\"refresh_requested\":true,\"physical_verified\":false,"
                 "\"current_measured\":false,\"faulted\":%s}",
                 (unsigned long)run, (unsigned long)boot, names[mode], (int)error, (int)restored,
                 before, target, before, (long long)entry_us, (long long)hold_us,
                 (long long)recovery_us, faulted ? "true" : "false");
    if (size < 0 || (size_t)size >= sizeof(report))
        strcpy(report, "{\"ok\":false,\"error\":\"report\"}");
}

void jelli_panel_trial_poll(JelliDisplayOutput *output, JelliPetEngine *engine)
{
    if (!pending)
        return;
    unsigned mode = pending;
    pending = NONE;
    uint64_t start = (uint64_t)esp_timer_get_time();
    execute(output, mode);
    engine->last_ms += ((uint64_t)esp_timer_get_time() - start) / 1000u;
    JelliInput ignored;
    for (unsigned i = 0; i < 8 && engine->platform.poll; ++i)
        if (!engine->platform.poll(engine->platform.ctx, &ignored))
            break;
}

#include "../boards/waveshare_31261.h"
#include "../experiments.h"
#include "touch_trial.h"
#include "panel_trial.h"
#include "imu_trial.h"
#include "sound_output.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

enum { NONE, DELAY, BLANK, PAUSE, OFF, PANEL, AUDIO, RESET50, RESET100, LIGHT, TOUCH, AUTO };
static const char *const modes[] = {"none",  "delay",   "blank",    "pause", "off",   "panel",
                                    "audio", "reset50", "reset100", "light", "touch", "auto"};
static unsigned pending;
/* Both owners access this under the BSP/LVGL display lock. */
static bool suppress_touch;
static uint32_t tick_delta, network_delta;
static uint64_t hold_us, recovery_us;
static char report[512] = "{\"ok\":true,\"mode\":\"none\"}";
/* Diagnostic scratch only; no mutation or persistence of the live pet. */
static JelliGame time_trial;

bool jelli_experiments_filter_touch(lv_event_code_t code)
{
    if (jelli_touch_trial_event(code))
        return true;
    if (!suppress_touch)
        return false;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        suppress_touch = false;
    return true;
}

void jelli_experiments_init(JelliTouchGuard *guard)
{
    jelli_power_probe_init(guard);
    jelli_imu_trial_init();
    ESP_LOGI("power", "diagnostics ready; no automatic sleep policy enabled");
}

static bool touch_command(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    if ((pending || jelli_panel_trial_busy()) && count >= 4 && !strcmp(words[2], "power") &&
        !strcmp(words[3], "touch")) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return true;
    }
    if (pending && count >= 4 && !strcmp(words[2], "power") && !strcmp(words[3], "panel")) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return true;
    }
    return jelli_touch_trial_command(debug, id, words, count) ||
           jelli_panel_trial_command(debug, id, words, count);
}

static bool reject_busy_run(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    bool run_command = count >= 4 && !strcmp(words[2], "power") &&
                       (!strcmp(words[3], "run") || (count >= 5 && !strcmp(words[4], "run")));
    if (run_command && (pending || jelli_touch_trial_busy() || jelli_panel_trial_busy() ||
                        jelli_imu_trial_busy())) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return true;
    }
    return false;
}

bool jelli_experiments_command(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id,
                               char **words, unsigned count)
{
    if (reject_busy_run(debug, id, words, count))
        return true;
    if (jelli_imu_trial_command(debug, id, words, count) || touch_command(debug, id, words, count))
        return true;
    if (count < 3u || strcmp(words[2], "power"))
        return false;
    if (count == 3u) {
        jelli_power_probe_report(debug, id,
                                 output->brightness_known ? (int)output->brightness_percent : -1);
    } else if (count == 4u && !strcmp(words[3], "result")) {
        jelli_debug_response(debug, id, report);
    } else if (count == 4u && !strcmp(words[3], "locks")) {
#ifdef CONFIG_PM_ENABLE
        esp_pm_dump_locks(stdout);
        jelli_debug_response(debug, id, "{\"ok\":true,\"pm_compiled\":true}");
#else
        jelli_debug_response(debug, id, "{\"ok\":true,\"pm_compiled\":false}");
#endif
    } else if (count == 5u && !strcmp(words[3], "run") && !debug->captured) {
        for (unsigned i = DELAY; i <= AUTO; ++i)
            if (!strcmp(words[4], modes[i]))
                pending = i;
        jelli_debug_response(debug, id,
                             pending ? "{\"ok\":true,\"pending\":true}"
                                     : "{\"ok\":false,\"error\":\"mode\"}");
    } else {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax_or_busy\"}");
    }
    return true;
}

static esp_err_t sleep_once(unsigned mode)
{
    esp_err_t result = gpio_wakeup_enable(BSP_LCD_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    if (result == ESP_OK)
        result = esp_sleep_enable_gpio_wakeup();
    if (result == ESP_OK)
        result = esp_sleep_enable_timer_wakeup(mode == TOUCH ? 10000000u : 100000u);
    if (result == ESP_OK && gpio_get_level(BSP_LCD_TOUCH_INT) == 0)
        result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK)
        result = esp_light_sleep_start();
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    (void)gpio_wakeup_disable(BSP_LCD_TOUCH_INT);
    return result;
}

static esp_err_t set_brightness(JelliDisplayOutput *output, int brightness)
{
    if (brightness < 0)
        return ESP_ERR_INVALID_STATE;
    return (esp_err_t)jelli_display_brightness(jelli_display_output_driver(output),
                                               (unsigned)brightness);
}

static esp_err_t restore_trial(JelliDisplayOutput *output, unsigned mode, int brightness,
                               bool panel_attempted, bool audio_closed, bool paused)
{
    esp_err_t result = ESP_OK;
    if (mode == OFF && output->transfer.panel)
        result = esp_lcd_panel_disp_on_off(output->transfer.panel, true);
    if (panel_attempted && output->transfer.panel)
        result = esp_lcd_panel_disp_sleep(output->transfer.panel, false);
    if (audio_closed && !jelli_sound_output_enable(true))
        result = ESP_FAIL;
    if (mode != DELAY && set_brightness(output, brightness) != ESP_OK)
        result = ESP_FAIL;
    if (paused && esp_lv_adapter_resume() != ESP_OK)
        result = ESP_FAIL;
    return result;
}

static esp_err_t auto_trial(void)
{
#ifdef CONFIG_PM_ENABLE
    esp_pm_config_t previous;
    esp_err_t result = esp_pm_get_configuration(&previous);
    if (result != ESP_OK)
        return result;
    esp_pm_config_t config = {.max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true};
    result = esp_pm_configure(&config);
    if (result == ESP_OK) {
        esp_pm_dump_locks(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_pm_dump_locks(stdout);
    }
    esp_err_t restored = esp_pm_configure(&previous);
    return result == ESP_OK ? restored : result;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t hold_trial(unsigned mode)
{
    if (mode == AUTO)
        return auto_trial();
    if (mode >= LIGHT)
        return sleep_once(mode);
    if (mode == RESET50 || mode == RESET100)
        return jelli_power_probe_reset(mode == RESET50 ? 50u : 100u);
    vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_OK;
}

static esp_err_t run_trial(unsigned mode, JelliDisplayOutput *output, esp_err_t *restore)
{
    int brightness = output->brightness_known ? (int)output->brightness_percent : -1;
    bool paused = false, audio_closed = false, panel_attempted = false;
    esp_err_t result = ESP_OK;
    if (mode >= PAUSE) {
        result = esp_lv_adapter_pause(2000);
        paused = result == ESP_OK;
    }
    /* Blocking command serializes after queued SPI colors before panel sleep. */
    if (result == ESP_OK && mode != DELAY)
        result = set_brightness(output, mode == BLANK ? 0 : brightness);
    if (result == ESP_OK && (mode == AUDIO || mode >= LIGHT)) {
        audio_closed = jelli_sound_output_enable(false);
        result = audio_closed ? ESP_OK : ESP_FAIL;
    }
    if (result == ESP_OK && mode == OFF)
        result = output->transfer.panel ? esp_lcd_panel_disp_on_off(output->transfer.panel, false)
                                        : ESP_ERR_INVALID_STATE;
    if (result == ESP_OK && (mode == PANEL || mode >= LIGHT)) {
        panel_attempted = true;
        result = output->transfer.panel ? esp_lcd_panel_disp_sleep(output->transfer.panel, true)
                                        : ESP_ERR_INVALID_STATE;
    }
    uint64_t hold_start = (uint64_t)esp_timer_get_time();
    uint32_t tick_start = lv_tick_get();
    uint32_t network_start = jelli_experiments_network_wakes();
#ifdef CONFIG_PM_ENABLE
    esp_pm_dump_locks(stdout);
#endif
    if (result == ESP_OK)
        result = hold_trial(mode);
    hold_us = (uint64_t)esp_timer_get_time() - hold_start;
    tick_delta = lv_tick_get() - tick_start;
    network_delta = jelli_experiments_network_wakes() - network_start;
    uint64_t recovery_start = (uint64_t)esp_timer_get_time();
    *restore = restore_trial(output, mode, brightness, panel_attempted, audio_closed, paused);
    recovery_us = (uint64_t)esp_timer_get_time() - recovery_start;
    output->refresh_requested = true;
    return result;
}

void jelli_experiments_poll(JelliDisplayOutput *output, JelliPetEngine *engine)
{
    jelli_imu_trial_poll();
    jelli_touch_trial_poll();
    jelli_panel_trial_poll(output, engine);
    if (!pending)
        return;
    unsigned mode = pending;
    pending = NONE;
    if (mode == TOUCH) {
        ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
        suppress_touch = true;
        bsp_display_unlock();
    }
    uint64_t start = (uint64_t)esp_timer_get_time();
    uint32_t irqs = jelli_power_probe_irqs();
    esp_err_t restore;
    ESP_LOGI("power", "begin %s; bounded timer fallback; pet time paused for diagnostic",
             modes[mode]);
    esp_err_t result = run_trial(mode, output, &restore);
    uint64_t end = (uint64_t)esp_timer_get_time();
    /* Exclude this explicit diagnostic from live progression, including captures. */
    engine->last_ms += (end - start) / 1000u;
    JelliInput ignored;
    for (unsigned n = 0; n < 8u && engine->platform.poll; ++n)
        if (!engine->platform.poll(engine->platform.ctx, &ignored))
            break;
    uint64_t before = engine->game.discarded_ms;
    time_trial = engine->game;
    time_trial.backlog_ms = 0;
    jelli_game_advance(&time_trial, 3000u);
    uint64_t dropped = time_trial.discarded_ms - before;
    time_trial = engine->game;
    time_trial.backlog_ms = 0;
    jelli_game_resume_begin(&time_trial, 3000u);
    (void)jelli_game_resume_step(&time_trial);
    uint64_t resumed_ticks = time_trial.ticks - engine->game.ticks;
    int size = snprintf(
        report, sizeof(report),
        "{\"ok\":true,\"mode\":\"%s\",\"error\":%d,\"restore_error\":%d,"
        "\"elapsed_us\":%llu,\"wake_cause\":%d,\"irq_delta\":%lu,"
        "\"live_3s_discarded_ms\":%llu,\"resume_3s_ticks\":%llu,\"hold_us\":%llu,"
        "\"recovery_us\":%llu,\"lv_tick_delta\":%lu,\"network_wakes\":%lu}",
        modes[mode], (int)result, (int)restore, (unsigned long long)(end - start),
        (mode == LIGHT || mode == TOUCH) && result == ESP_OK ? (int)esp_sleep_get_wakeup_cause()
                                                             : 0,
        (unsigned long)(jelli_power_probe_irqs() - irqs), (unsigned long long)dropped,
        (unsigned long long)resumed_ticks, (unsigned long long)hold_us,
        (unsigned long long)recovery_us, (unsigned long)tick_delta, (unsigned long)network_delta);
    if (size < 0 || (size_t)size >= sizeof(report))
        strcpy(report, "{\"ok\":false,\"error\":\"report\"}");
    ESP_LOGI("power", "%s", report);
}

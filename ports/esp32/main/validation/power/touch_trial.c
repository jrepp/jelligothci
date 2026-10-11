#include "touch_trial.h"
#include "jelli/gesture.h"
#include "../boards/waveshare_31261.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

enum { LEGACY, PROPAGATE, GUARD, STEP_COUNT = 8 };
/* Released, press, failed read, continued hold, release, fresh press/release, idle. */
static const unsigned contacts[STEP_COUNT] = {0, 1, 1, 1, 0, 1, 0, 0};
static struct {
    bool active, armed;
    JelliTouchGuard guard;
    unsigned mode, step, consumed, taps, false_taps, presses, releases;
    uint32_t run, period, irq_start;
    int64_t started, notified, deadline, delay[STEP_COUNT];
    JelliGesture gesture;
} trial;
_Static_assert(sizeof(trial) <= 192u, "Touch trial state exceeds 192 bytes");
static char report[768] = "{\"ok\":true,\"status\":\"idle\"}";

bool jelli_touch_trial_event(lv_event_code_t code)
{
    if (!trial.active)
        return false;
    if (code == LV_EVENT_PRESSED) {
        ++trial.presses;
        jelli_gesture_begin(&trial.gesture, 233, 233);
    } else if (code == LV_EVENT_PRESS_LOST) {
        trial.gesture.active = false;
    } else if (code == LV_EVENT_RELEASED) {
        ++trial.releases;
        JelliInput input;
        if (jelli_gesture_end(&trial.gesture, 233, 233, &input)) {
            ++trial.taps;
            if (trial.step < 6)
                ++trial.false_taps;
        }
    }
    return true; /* Synthetic samples never enter the live game queue. */
}

esp_err_t jelli_touch_trial_read(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points,
                                 uint8_t *count, uint8_t capacity, void *ctx)
{
    if (!trial.active)
        return jelli_touch_input_read(tp, points, count, capacity, ctx);
    *count = 0;
    if (!trial.armed || !capacity)
        return ESP_OK;
    trial.armed = false;
    trial.delay[trial.step] = esp_timer_get_time() - trial.notified;
    trial.consumed = trial.step + 1;
    bool failed = trial.step == 2;
    if (contacts[trial.step]) {
        points[0] = (esp_lcd_touch_point_data_t){.x = 233, .y = 233, .strength = 1};
        *count = 1;
    }
    if (trial.mode == GUARD)
        jelli_touch_guard_apply(&trial.guard, failed, count);
    if (failed && trial.mode != LEGACY) {
        *count = 0;
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

bool jelli_touch_trial_busy(void)
{
    if (bsp_display_lock(100) != ESP_OK)
        return true;
    bool active = trial.active;
    bsp_display_unlock();
    return active;
}

static void finish(bool timeout)
{
    lv_indev_t *indev = bsp_display_get_input_dev();
    lv_timer_set_period(lv_indev_get_read_timer(indev), CONFIG_LV_DEF_REFR_PERIOD);
    lv_indev_reset(indev, NULL);
    int size = snprintf(
        report, sizeof(report),
        "{\"ok\":true,\"status\":\"%s\",\"run\":%lu,\"mode\":%u,\"period_ms\":%lu,"
        "\"consumed\":%u,\"taps\":%u,\"false_taps\":%u,\"presses\":%u,\"releases\":%u,"
        "\"synthetic\":true,\"restored_period_ms\":%u,\"notify_to_read_us\":[%lld,%lld,%lld,%lld,"
        "%lld,%lld,%lld,%lld]}",
        timeout                                       ? "timeout"
        : jelli_power_probe_irqs() != trial.irq_start ? "contaminated"
                                                      : "complete",
        (unsigned long)trial.run, trial.mode, (unsigned long)trial.period, trial.consumed,
        trial.taps, trial.false_taps, trial.presses, trial.releases,
        (unsigned)CONFIG_LV_DEF_REFR_PERIOD, (long long)trial.delay[0], (long long)trial.delay[1],
        (long long)trial.delay[2], (long long)trial.delay[3], (long long)trial.delay[4],
        (long long)trial.delay[5], (long long)trial.delay[6], (long long)trial.delay[7]);
    if (size < 0 || (size_t)size >= sizeof(report))
        strcpy(report, "{\"ok\":false,\"error\":\"report\"}");
    trial.active = false;
}

void jelli_touch_trial_poll(void)
{
    if (bsp_display_lock(50) != ESP_OK)
        return;
    if (trial.active) {
        int64_t now = esp_timer_get_time();
        if (now > trial.deadline || trial.consumed == STEP_COUNT) {
            finish(now > trial.deadline);
        } else if (!trial.armed) {
            trial.step = trial.consumed;
            trial.armed = true;
            trial.notified = now;
            (void)esp_lv_adapter_touch_notify_interrupt(bsp_display_get_input_dev());
        }
    }
    bsp_display_unlock();
}

static bool start(const char *mode, uint32_t period)
{
    unsigned selected = !strcmp(mode, "legacy")      ? LEGACY
                        : !strcmp(mode, "propagate") ? PROPAGATE
                                                     : GUARD;
    if ((selected == GUARD && strcmp(mode, "guard")) ||
        (period != 33 && period != 16 && period != 10) || trial.active)
        return false;
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (lv_indev_get_state(indev) != LV_INDEV_STATE_RELEASED)
        return false;
    uint32_t run = trial.run == UINT32_MAX ? 1u : trial.run + 1u;
    memset(&trial, 0, sizeof(trial));
    trial.run = run;
    trial.active = true;
    trial.guard.gesture = &trial.gesture;
    trial.mode = selected;
    trial.period = period;
    trial.irq_start = jelli_power_probe_irqs();
    trial.started = esp_timer_get_time();
    trial.deadline = trial.started + 3000000;
    lv_timer_set_period(lv_indev_get_read_timer(indev), period);
    strcpy(report, "{\"ok\":true,\"status\":\"running\"}");
    return true;
}

bool jelli_touch_trial_command(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    if (count < 4 || strcmp(words[2], "power") || strcmp(words[3], "touch"))
        return false;
    if (bsp_display_lock(100) != ESP_OK) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"lock\"}");
        return true;
    }
    uint32_t period;
    if (count == 5 && !strcmp(words[4], "result"))
        jelli_debug_response(debug, id, report);
    else if (count == 7 && !strcmp(words[4], "run") && jelli_debug_number(words[6], &period) &&
             start(words[5], period)) {
        char accepted[96];
        (void)snprintf(accepted, sizeof(accepted), "{\"ok\":true,\"pending\":true,\"run\":%lu}",
                       (unsigned long)trial.run);
        jelli_debug_response(debug, id, accepted);
    } else
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax_or_busy\"}");
    bsp_display_unlock();
    return true;
}

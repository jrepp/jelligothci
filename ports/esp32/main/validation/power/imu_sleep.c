#include "imu_sleep.h"
#include "power_trial.h"
#include "network.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

/* GPIO21/INT2, active high, exact SKU31261. Always pair with a finite timer. */
static struct {
    esp_err_t cleanup, host_restore;
    int wake_cause, wake_level;
    bool attempted, completed, already_active;
    int64_t sleep_us;
} state;
_Static_assert(sizeof(state) <= 40, "IMU sleep state exceeds budget");

void jelli_imu_sleep_reset(void)
{
    memset(&state, 0, sizeof(state));
    state.wake_level = -1;
}

bool jelli_imu_sleep_completed(void) { return state.completed; }

static void cleanup_error(esp_err_t error)
{
    if (state.cleanup == ESP_OK)
        state.cleanup = error;
}

static esp_err_t sleep_hold(void)
{
    bool pin = false, gpio = false, timer = false;
    esp_err_t error = gpio_wakeup_enable(GPIO_NUM_21, GPIO_INTR_HIGH_LEVEL);
    pin = error == ESP_OK;
    if (error == ESP_OK) {
        error = esp_sleep_enable_gpio_wakeup();
        gpio = error == ESP_OK;
    }
    if (error == ESP_OK) {
        error = esp_sleep_enable_timer_wakeup(UINT64_C(10000000));
        timer = error == ESP_OK;
    }
    /* Last check immediately before the API call; a later edge wakes normally. */
    if (error == ESP_OK && gpio_get_level(GPIO_NUM_21) != 0) {
        state.already_active = true;
        error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK) {
        state.attempted = true;
        int64_t start = esp_timer_get_time();
        error = esp_light_sleep_start();
        state.sleep_us = esp_timer_get_time() - start;
        state.wake_level = gpio_get_level(GPIO_NUM_21);
        if (error == ESP_OK) {
            state.wake_cause = (int)esp_sleep_get_wakeup_cause();
            state.completed = state.wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED;
        }
    }
    if (timer)
        cleanup_error(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER));
    if (gpio)
        cleanup_error(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO));
    if (pin)
        cleanup_error(gpio_wakeup_disable(GPIO_NUM_21));
    return error;
}

esp_err_t jelli_imu_sleep_run(JelliDisplayOutput *output, JelliPetEngine *engine)
{
    if (!jelli_network_idle_offline())
        return ESP_ERR_INVALID_STATE;
    if (gpio_get_level(GPIO_NUM_21) != 0) {
        state.already_active = true;
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = jelli_power_sleep_trial(output, engine, sleep_hold, &state.host_restore);
    if (error != ESP_OK)
        return error;
    return state.cleanup != ESP_OK ? state.cleanup : state.host_restore;
}

void jelli_imu_sleep_report(char *report, size_t capacity)
{
    size_t length = strlen(report);
    if (!length || report[length - 1u] != '}')
        return;
    int size = snprintf(report + length - 1u, capacity - length + 1u,
                        ",\"sleep_attempted\":%s,\"already_active\":%s,"
                        "\"wake_cause\":%d,\"wake_level\":%d,\"sleep_us\":%lld,"
                        "\"wake_cleanup_error\":%d,\"host_restore_error\":%d}",
                        state.attempted ? "true" : "false", state.already_active ? "true" : "false",
                        state.wake_cause, state.wake_level, (long long)state.sleep_us,
                        (int)state.cleanup, (int)state.host_restore);
    if (size < 0 || (size_t)size >= capacity - length + 1u)
        (void)snprintf(report, capacity, "{\"ok\":false,\"error\":\"report\"}");
}

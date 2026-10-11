#include "imu_trial.h"
#include "../boards/qmi_probe.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

enum { NONE, IDENTITY, QUIET, MOTION, QUIET_INT2, MOTION_INT2 };
static const char *const names[] = {"none",   "identity",   "quiet",
                                    "motion", "quiet-int2", "motion-int2"};
static unsigned pending, active;
static uint32_t boot, run;
static bool faulted;
static esp_err_t open_error;
static int64_t started, deadline, settle_until;
static bool settling;
static int initial_level, observed_level;
static JelliQmiProbe probe;
static char report[1200] = "{\"ok\":true,\"status\":\"idle\"}";
_Static_assert(sizeof(probe) <= 80, "IMU scratch exceeds budget");

void jelli_imu_trial_init(void)
{
    boot = esp_random();
    open_error = jelli_qmi_open(bsp_i2c_get_handle());
    /* Exact SKU31261 schematic: QMI INT2 directly reaches GPIO21. */
    gpio_config_t config = {.pin_bit_mask = UINT64_C(1) << 21,
                            .mode = GPIO_MODE_INPUT,
                            .pull_up_en = GPIO_PULLUP_DISABLE,
                            .pull_down_en = GPIO_PULLDOWN_DISABLE,
                            .intr_type = GPIO_INTR_DISABLE};
    esp_err_t error = gpio_config(&config);
    if (error != ESP_OK)
        open_error = error;
}

bool jelli_imu_trial_busy(void) { return pending || active; }

bool jelli_imu_trial_command(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    if (count < 4 || strcmp(words[2], "power") || strcmp(words[3], "imu"))
        return false;
    if (count == 5 && !strcmp(words[4], "result")) {
        jelli_debug_response(debug, id, report);
        return true;
    }
    if (jelli_imu_trial_busy() || faulted || debug->captured) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy_or_faulted\"}");
        return true;
    }
    if (count == 6 && !strcmp(words[4], "run"))
        for (unsigned i = IDENTITY; i <= MOTION_INT2; ++i)
            if (!strcmp(words[5], names[i]))
                pending = i;
    if (!pending) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"mode\"}");
        return true;
    }
    run = run == UINT32_MAX ? 1u : run + 1u;
    char accepted[112];
    (void)snprintf(accepted, sizeof(accepted),
                   "{\"ok\":true,\"pending\":true,\"run\":%lu,\"boot\":%lu}", (unsigned long)run,
                   (unsigned long)boot);
    jelli_debug_response(debug, id, accepted);
    return true;
}

static void hex_bytes(const uint8_t *bytes, unsigned count, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < count; ++i) {
        out[i * 2u] = digits[bytes[i] >> 4u];
        out[i * 2u + 1u] = digits[bytes[i] & 15u];
    }
    out[count * 2u] = '\0';
}

static esp_err_t pull_probe(int *down, int *up)
{
    esp_err_t error = gpio_set_pull_mode(GPIO_NUM_21, GPIO_PULLDOWN_ONLY);
    vTaskDelay(pdMS_TO_TICKS(5));
    *down = gpio_get_level(GPIO_NUM_21);
    esp_err_t next = gpio_set_pull_mode(GPIO_NUM_21, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(5));
    *up = gpio_get_level(GPIO_NUM_21);
    esp_err_t restored = gpio_set_pull_mode(GPIO_NUM_21, GPIO_FLOATING);
    return error != ESP_OK ? error : next != ESP_OK ? next : restored;
}

static void finish(esp_err_t error)
{
    unsigned mode = active;
    int64_t elapsed = esp_timer_get_time() - started;
    observed_level = gpio_get_level(GPIO_NUM_21);
    if (mode != IDENTITY && probe.wom_attempted) {
        esp_err_t ack = jelli_qmi_ack_motion(&probe);
        if (error == ESP_OK)
            error = ack;
    }
    int after_ack = gpio_get_level(GPIO_NUM_21), pull_down, pull_up;
    esp_err_t pulls = pull_probe(&pull_down, &pull_up);
    if (error == ESP_OK)
        error = pulls;
    esp_err_t restored = jelli_qmi_restore(&probe);
    faulted = error != ESP_OK || restored != ESP_OK;
    char before[21], after[21], armed[21], firmware[7], usid[13];
    hex_bytes(probe.before, JELLI_QMI_REG_COUNT, before);
    hex_bytes(probe.armed, JELLI_QMI_REG_COUNT, armed);
    hex_bytes(probe.after, JELLI_QMI_REG_COUNT, after);
    hex_bytes(probe.firmware, 3, firmware);
    hex_bytes(probe.usid, 6, usid);
    int size = snprintf(
        report, sizeof(report),
        "{\"ok\":true,\"status\":\"complete\",\"mode\":\"%s\",\"boot\":%lu,\"run\":%lu,"
        "\"error\":%d,\"restore_error\":%d,\"restored\":%s,\"faulted\":%s,"
        "\"who\":%u,\"revision\":%u,\"firmware\":\"%s\",\"usid\":\"%s\","
        "\"before\":\"%s\",\"after\":\"%s\",\"armed\":\"%s\",\"commands\":%lu,\"ack_status\":%u,"
        "\"status1\":%u,\"status_valid\":%s,\"initial_level\":%d,\"observed_level\":%d,"
        "\"after_ack_level\":%d,\"pull_down\":%d,\"pull_up\":%d,\"elapsed_us\":%lld,\"mcu_slept\":"
        "false,\"current_"
        "measured\":false}",
        names[mode], (unsigned long)boot, (unsigned long)run, (int)error, (int)restored,
        probe.restored ? "true" : "false", faulted ? "true" : "false", probe.who, probe.revision,
        firmware, usid, before, after, armed, (unsigned long)probe.commands, probe.ack_status,
        probe.status, probe.status_valid ? "true" : "false", initial_level, observed_level,
        after_ack, pull_down, pull_up, (long long)elapsed);
    if (size < 0 || (size_t)size >= sizeof(report))
        strcpy(report, "{\"ok\":false,\"error\":\"report\"}");
    active = NONE;
}

void jelli_imu_trial_poll(void)
{
    if (pending) {
        active = pending;
        pending = NONE;
        probe = (JelliQmiProbe){0};
        started = esp_timer_get_time();
        initial_level = gpio_get_level(GPIO_NUM_21);
        esp_err_t error = open_error;
        if (error == ESP_OK)
            error = jelli_qmi_begin(&probe, active != IDENTITY, active >= QUIET_INT2);
        if (error != ESP_OK || active == IDENTITY) {
            finish(error);
            return;
        }
        settling = true;
        settle_until = esp_timer_get_time() + 1000000;
        (void)snprintf(
            report, sizeof(report),
            "{\"ok\":true,\"status\":\"settling\",\"mode\":\"%s\",\"boot\":%lu,\"run\":%lu}",
            names[active], (unsigned long)boot, (unsigned long)run);
    }
    if (active && settling && esp_timer_get_time() >= settle_until) {
        settling = false;
        initial_level = gpio_get_level(GPIO_NUM_21);
        if (initial_level != 0) {
            finish(ESP_ERR_INVALID_STATE);
            return;
        }
        deadline =
            esp_timer_get_time() + ((active == QUIET || active == QUIET_INT2) ? 2000000 : 20000000);
        (void)snprintf(
            report, sizeof(report),
            "{\"ok\":true,\"status\":\"armed\",\"mode\":\"%s\",\"boot\":%lu,\"run\":%lu}",
            names[active], (unsigned long)boot, (unsigned long)run);
    }
    if (active && !settling &&
        (gpio_get_level(GPIO_NUM_21) != 0 || esp_timer_get_time() >= deadline))
        finish(ESP_OK);
}

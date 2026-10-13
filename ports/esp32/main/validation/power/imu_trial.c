#include "imu_trial.h"
#include "imu_sleep.h"
#include "../boards/qmi_probe.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

enum { NONE, IDENTITY, QUIET, MOTION, QUIET_INT2, MOTION_INT2, REARM_INT2, SLEEP_INT2 };
enum { REARM_WAIT_MOTION, REARM_SETTLE, REARM_WAIT_IDLE };
enum {
    MOTION_TIMEOUT_US = 20000000,
    REARM_SETTLE_US = 3000000,
    REARM_ACK_TIMEOUT_US = 1000000,
    REARM_IDLE_US = 100000
};
static const char *const names[] = {"none",       "identity",    "quiet",      "motion",
                                    "quiet-int2", "motion-int2", "rearm-int2", "sleep-int2"};
static unsigned pending, active;
static uint32_t boot, run;
static bool faulted;
static esp_err_t open_error;
static int64_t started, deadline, settle_until;
static bool settling;
static int initial_level, observed_level;
static JelliQmiProbe probe;
/* Two awake motion/ack episodes, without reconfiguring the sensor between them. */
static struct {
    unsigned phase, completed;
    int motion_level[2], ack_level[2], idle_level[2];
    uint8_t motion_status[2];
    uint32_t ack_wait_us[2];
    int64_t ack_started, low_since;
} rearm;
static char report[1500] = "{\"ok\":true,\"status\":\"idle\"}";
_Static_assert(sizeof(probe) <= 80, "IMU scratch exceeds budget");
_Static_assert(sizeof(rearm) <= 64, "IMU rearm state exceeds budget");

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
        for (unsigned i = IDENTITY; i <= SLEEP_INT2; ++i)
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
    if (mode != IDENTITY && mode != REARM_INT2 && probe.wom_attempted) {
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
        "%s,\"current_measured\":false,\"completed_events\":%u,"
        "\"motion_levels\":[%d,%d],\"motion_status\":[%u,%u],"
        "\"ack_levels\":[%d,%d],\"idle_levels\":[%d,%d],\"ack_wait_us\":[%lu,%lu]}",
        names[mode], (unsigned long)boot, (unsigned long)run, (int)error, (int)restored,
        probe.restored ? "true" : "false", faulted ? "true" : "false", probe.who, probe.revision,
        firmware, usid, before, after, armed, (unsigned long)probe.commands, probe.ack_status,
        probe.status, probe.status_valid ? "true" : "false", initial_level, observed_level,
        after_ack, pull_down, pull_up, (long long)elapsed,
        jelli_imu_sleep_completed() ? "true" : "false", rearm.completed, rearm.motion_level[0],
        rearm.motion_level[1], rearm.motion_status[0], rearm.motion_status[1], rearm.ack_level[0],
        rearm.ack_level[1], rearm.idle_level[0], rearm.idle_level[1],
        (unsigned long)rearm.ack_wait_us[0], (unsigned long)rearm.ack_wait_us[1]);
    if (size < 0 || (size_t)size >= sizeof(report))
        strcpy(report, "{\"ok\":false,\"error\":\"report\"}");
    if (mode == SLEEP_INT2)
        jelli_imu_sleep_report(report, sizeof(report));
    active = NONE;
}

static void rearm_status(const char *status)
{
    (void)snprintf(report, sizeof(report),
                   "{\"ok\":true,\"status\":\"%s\",\"mode\":\"rearm-int2\","
                   "\"boot\":%lu,\"run\":%lu,\"event\":%u}",
                   status, (unsigned long)boot, (unsigned long)run, rearm.completed + 1u);
}

static void rearm_poll(void)
{
    int64_t now = esp_timer_get_time();
    unsigned index = rearm.completed; /* finish stops polling before index reaches two. */
    int level = gpio_get_level(GPIO_NUM_21);
    if (rearm.phase == REARM_WAIT_MOTION) {
        if (now >= deadline)
            finish(ESP_ERR_TIMEOUT);
        else if (level != 0) {
            rearm.motion_level[index] = level;
            rearm.phase = REARM_SETTLE;
            deadline = now + REARM_SETTLE_US; /* Set the board down before the single ACK. */
            rearm_status("settling");
        }
    } else if (rearm.phase == REARM_SETTLE && now >= deadline) {
        esp_err_t error = jelli_qmi_ack_motion(&probe);
        rearm.motion_status[index] = probe.status;
        rearm.ack_level[index] = gpio_get_level(GPIO_NUM_21);
        if (error != ESP_OK || !probe.status_valid || !(probe.status & 4u)) {
            finish(error != ESP_OK ? error : ESP_ERR_INVALID_RESPONSE);
            return;
        }
        rearm.ack_started = esp_timer_get_time();
        rearm.low_since = 0;
        deadline = rearm.ack_started + REARM_ACK_TIMEOUT_US;
        rearm.phase = REARM_WAIT_IDLE;
    } else if (rearm.phase == REARM_WAIT_IDLE) {
        if (now >= deadline) {
            finish(ESP_ERR_TIMEOUT);
            return;
        }
        if (level != 0)
            rearm.low_since = 0;
        else if (!rearm.low_since)
            rearm.low_since = now;
        if (rearm.low_since && now - rearm.low_since >= REARM_IDLE_US) {
            rearm.idle_level[index] = 0;
            rearm.ack_wait_us[index] = (uint32_t)(now - rearm.ack_started);
            if (++rearm.completed == 2u)
                finish(ESP_OK);
            else {
                rearm.phase = REARM_WAIT_MOTION;
                deadline = now + MOTION_TIMEOUT_US;
                rearm_status("armed");
            }
        }
    }
}

void jelli_imu_trial_poll(JelliDisplayOutput *output, JelliPetEngine *engine)
{
    if (pending) {
        active = pending;
        pending = NONE;
        probe = (JelliQmiProbe){0};
        jelli_imu_sleep_reset();
        memset(&rearm, 0, sizeof(rearm));
        rearm.idle_level[0] = rearm.idle_level[1] = -1;
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
        if (active == SLEEP_INT2) {
            finish(jelli_imu_sleep_run(output, engine));
            return;
        }
        if (initial_level != 0) {
            finish(ESP_ERR_INVALID_STATE);
            return;
        }
        deadline = esp_timer_get_time() +
                   ((active == QUIET || active == QUIET_INT2) ? 2000000 : MOTION_TIMEOUT_US);
        (void)snprintf(
            report, sizeof(report),
            "{\"ok\":true,\"status\":\"armed\",\"mode\":\"%s\",\"boot\":%lu,\"run\":%lu}",
            names[active], (unsigned long)boot, (unsigned long)run);
        if (active == REARM_INT2)
            rearm_status("armed");
    }
    if (active == REARM_INT2 && !settling) {
        rearm_poll();
        return;
    }
    if (active && !settling &&
        (gpio_get_level(GPIO_NUM_21) != 0 || esp_timer_get_time() >= deadline))
        finish(ESP_OK);
}

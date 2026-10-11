#include "pipeline_profile.h"
#ifdef CONFIG_JELLI_POWER_EXPERIMENTS
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { SAMPLES = 128 };
typedef struct {
    uint32_t values[SAMPLES];
    uint32_t count, maximum;
    uint64_t total_us, bytes;
} Timing;
static DRAM_ATTR Timing timings[JELLI_PROFILE_COUNT];
static Timing snapshot; /* Engine-owned report scratch; never put on the task stack. */
static DRAM_ATTR portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR bool active, expired;
static DRAM_ATTR uint64_t submitted_us, expires_us;
static uint32_t generation, boot;
static const char *const names[] = {"engine", "lock", "copy", "check", "swap", "stage", "transfer"};
_Static_assert(sizeof(timings) + sizeof(snapshot) <= 4608u, "Profile exceeds 4.5 KiB");

static void IRAM_ATTR expire(void)
{
    if (active && (uint64_t)esp_timer_get_time() >= expires_us) {
        active = false;
        expired = true;
        submitted_us = 0u;
    }
}

bool jelli_profile_active(void)
{
    portENTER_CRITICAL(&lock);
    expire();
    bool enabled = active;
    portEXIT_CRITICAL(&lock);
    return enabled;
}

uint64_t jelli_profile_now(void) { return (uint64_t)esp_timer_get_time(); }

/* Both callers hold lock; ISR work is constant and never formats output. */
static void IRAM_ATTR record(JelliProfileStage stage, uint64_t elapsed, uint32_t bytes)
{
    expire();
    Timing *timing = &timings[stage];
    if (!active || timing->count == UINT32_MAX)
        return;
    uint32_t value = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
    timing->values[timing->count % SAMPLES] = value;
    ++timing->count;
    timing->total_us += value;
    timing->bytes += bytes;
    if (value > timing->maximum)
        timing->maximum = value;
}

void jelli_profile_record(JelliProfileStage stage, uint64_t start, uint32_t bytes)
{
    uint64_t end = jelli_profile_now();
    portENTER_CRITICAL(&lock);
    if ((unsigned)stage < JELLI_PROFILE_COUNT && end >= start)
        record(stage, end - start, bytes);
    portEXIT_CRITICAL(&lock);
}

void jelli_profile_submit(void)
{
    portENTER_CRITICAL(&lock);
    submitted_us = active ? jelli_profile_now() : 0u;
    portEXIT_CRITICAL(&lock);
}

void jelli_profile_submit_failed(void)
{
    portENTER_CRITICAL(&lock);
    submitted_us = 0u;
    portEXIT_CRITICAL(&lock);
}

/* Called by the pinned adapter before its original DMA-completion handling. */
void IRAM_ATTR jelli_profile_color_done(void)
{
    uint64_t end = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL_ISR(&lock);
    if (submitted_us && end >= submitted_us)
        record(JELLI_PROFILE_TRANSFER, end - submitted_us, 0u);
    submitted_us = 0u;
    portEXIT_CRITICAL_ISR(&lock);
}

/* Adapter default-flush hook, after stride packing and around byte swap only. */
void jelli_profile_swap(uint64_t start) { jelli_profile_record(JELLI_PROFILE_SWAP, start, 0u); }

static void report(JelliDebug *debug, uint32_t id, unsigned stage)
{
    portENTER_CRITICAL(&lock);
    snapshot = timings[stage];
    expire();
    bool enabled = active;
    bool timed_out = expired;
    portEXIT_CRITICAL(&lock);
    unsigned used = snapshot.count < SAMPLES ? snapshot.count : SAMPLES;
    for (unsigned i = 1u; i < used; ++i) {
        uint32_t value = snapshot.values[i];
        unsigned j = i;
        while (j && snapshot.values[j - 1u] > value) {
            snapshot.values[j] = snapshot.values[j - 1u];
            --j;
        }
        snapshot.values[j] = value;
    }
    char body[384];
    int size = snprintf(body, sizeof(body),
                        "{\"ok\":true,\"run\":%" PRIu32 ",\"boot\":%" PRIu32
                        ",\"active\":%s,\"expired\":%s,\"stage\":\"%s\","
                        "\"count\":%" PRIu32 ",\"window\":%u,\"p50_us\":%" PRIu32 ","
                        "\"p95_us\":%" PRIu32 ",\"max_us\":%" PRIu32 ","
                        "\"total_us\":%" PRIu64 ",\"bytes\":%" PRIu64 "}",
                        generation, boot, enabled ? "true" : "false", timed_out ? "true" : "false",
                        names[stage], snapshot.count, used,
                        used ? snapshot.values[(used * 50u + 99u) / 100u - 1u] : 0u,
                        used ? snapshot.values[(used * 95u + 99u) / 100u - 1u] : 0u,
                        snapshot.maximum, snapshot.total_us, snapshot.bytes);
    if (size > 0 && (size_t)size < sizeof(body))
        jelli_debug_response(debug, id, body);
}

bool jelli_profile_command(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    if (count < 4u || strcmp(words[2], "display") || strcmp(words[3], "profile"))
        return false;
    if (!boot)
        boot = esp_random();
    if (count == 5u && (!strcmp(words[4], "start") || !strcmp(words[4], "stop"))) {
        bool start = !strcmp(words[4], "start");
        /* Engine thread and LVGL mutex serialize reset with task-side samples. */
        portENTER_CRITICAL(&lock);
        active = false;
        submitted_us = 0u;
        portEXIT_CRITICAL(&lock);
        if (start && generation != UINT32_MAX) {
            memset(timings, 0, sizeof(timings));
            ++generation;
            portENTER_CRITICAL(&lock);
            expires_us = jelli_profile_now() + UINT64_C(60000000);
            expired = false;
            active = true;
            portEXIT_CRITICAL(&lock);
        }
        report(debug, id, JELLI_PROFILE_ENGINE);
    } else if (count == 5u) {
        for (unsigned stage = 0; stage < JELLI_PROFILE_COUNT; ++stage) {
            if (!strcmp(words[4], names[stage])) {
                report(debug, id, stage);
                return true;
            }
        }
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"stage\"}");
    } else
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax\"}");
    return true;
}

#endif

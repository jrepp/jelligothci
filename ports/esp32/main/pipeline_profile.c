#include "pipeline_profile.h"
#ifdef CONFIG_JELLI_POWER_EXPERIMENTS
#include "jelli/pet_ui.h"
#include "lvgl_copy_trial.h"
#include "esp_err.h"
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
static const char *const names[] = {
    "engine",     "lock",      "copy",       "check",         "swap",           "stage",
    "transfer",   "render",    "lvgl",       "lvgl_wait",     "lvgl_flush",     "lvgl_work",
    "lvgl_image", "lvgl_fill", "lvgl_tasks", "lvgl_dispatch", "lvgl_draw_wait", "lvgl_events"};
/* LVGL-task event state, serialized by the display mutex. Flush and wait
 * intervals are disjoint in the pinned LVGL partial-render path. */
static uint64_t lvgl_started_us, flush_started_us, wait_started_us;
static uint64_t frame_flush_us, frame_wait_us;
static uint32_t frame_bytes;
static void record_elapsed(JelliProfileStage stage, uint64_t elapsed, uint32_t bytes);
_Static_assert(sizeof(timings) + sizeof(snapshot) <= 10240u, "Profile exceeds 10 KiB");
_Static_assert(sizeof(names) / sizeof(names[0]) == JELLI_PROFILE_COUNT, "Stage names missing");

/* GNU ld wrapping is enabled only in the ESP32 power image. The original
 * renderer and portable engine remain unchanged; no second renderer is used. */
void __real_jelli_pet_render(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui,
                             uint64_t animation_ms, bool paused);
void __wrap_jelli_pet_render(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui,
                             uint64_t animation_ms, bool paused);
void __wrap_jelli_pet_render(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui,
                             uint64_t animation_ms, bool paused)
{
    uint64_t start = jelli_profile_now();
    __real_jelli_pet_render(surface, game, ui, animation_ms, paused);
    jelli_profile_record(JELLI_PROFILE_RENDER, start, 0u);
}

static void lvgl_event(lv_event_t *event)
{
    uint64_t now = jelli_profile_now();
    switch (lv_event_get_code(event)) {
    case LV_EVENT_RENDER_START:
        lvgl_started_us = now;
        frame_flush_us = frame_wait_us = 0u;
        frame_bytes = 0u;
        break;
    case LV_EVENT_FLUSH_START:
        flush_started_us = now;
        break;
    case LV_EVENT_FLUSH_FINISH:
        if (flush_started_us && now >= flush_started_us) {
            const lv_area_t *area = lv_event_get_param(event);
            uint32_t bytes = area ? (uint32_t)lv_area_get_size(area) * 2u : 0u;
            uint64_t elapsed = now - flush_started_us;
            if (lvgl_started_us) {
                record_elapsed(JELLI_PROFILE_LVGL_FLUSH, elapsed, bytes);
                frame_flush_us += elapsed;
                frame_bytes += bytes;
            }
        }
        flush_started_us = 0u;
        break;
    case LV_EVENT_FLUSH_WAIT_START:
        wait_started_us = now;
        break;
    case LV_EVENT_FLUSH_WAIT_FINISH:
        if (wait_started_us && now >= wait_started_us) {
            uint64_t elapsed = now - wait_started_us;
            if (lvgl_started_us) {
                record_elapsed(JELLI_PROFILE_LVGL_WAIT, elapsed, 0u);
                frame_wait_us += elapsed;
            }
        }
        wait_started_us = 0u;
        break;
    case LV_EVENT_RENDER_READY:
        if (lvgl_started_us && now >= lvgl_started_us) {
            uint64_t elapsed = now - lvgl_started_us;
            record_elapsed(JELLI_PROFILE_LVGL, elapsed, frame_bytes);
            uint64_t excluded = frame_flush_us + frame_wait_us;
            if (excluded <= elapsed)
                record_elapsed(JELLI_PROFILE_LVGL_WORK, elapsed - excluded, frame_bytes);
        }
        lvgl_started_us = 0u;
        break;
    default:
        break;
    }
}

void jelli_profile_attach(lv_display_t *display)
{
    uint32_t before = lv_display_get_event_count(display);
    lv_display_add_event_cb(display, lvgl_event, LV_EVENT_ALL, NULL);
    ESP_ERROR_CHECK(lv_display_get_event_count(display) > before ? ESP_OK : ESP_ERR_NO_MEM);
}

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

static void record_elapsed(JelliProfileStage stage, uint64_t elapsed, uint32_t bytes)
{
    portENTER_CRITICAL(&lock);
    if ((unsigned)stage < JELLI_PROFILE_COUNT)
        record(stage, elapsed, bytes);
    portEXIT_CRITICAL(&lock);
}

void jelli_profile_record(JelliProfileStage stage, uint64_t start, uint32_t bytes)
{
    uint64_t end = jelli_profile_now();
    if (end >= start)
        record_elapsed(stage, end - start, bytes);
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

static void copy_mode(JelliDebug *debug, uint32_t id, const char *method)
{
    bool libc_copy = !strcmp(method, "libc");
    if ((!libc_copy && strcmp(method, "builtin")) || jelli_profile_active()) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"mode_or_busy\"}");
        return;
    }
    if (!jelli_lvgl_copy_qualify()) {
        jelli_lvgl_copy_mode(false);
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"copy_equivalence\"}");
        return;
    }
    jelli_lvgl_copy_mode(libc_copy);
    jelli_debug_response(debug, id,
                         libc_copy ? "{\"ok\":true,\"memcpy\":\"libc\"}"
                                   : "{\"ok\":true,\"memcpy\":\"builtin\"}");
}

static void overlay_mode(JelliDebug *debug, uint32_t id, const char *method)
{
    bool skipped = !strcmp(method, "skip");
    if ((!skipped && strcmp(method, "baseline")) || jelli_profile_active() ||
        !jelli_profile_overlay_mode(skipped)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"overlay_guard_or_busy\"}");
        return;
    }
    jelli_debug_response(debug, id,
                         skipped ? "{\"ok\":true,\"overlays\":\"skip\"}"
                                 : "{\"ok\":true,\"overlays\":\"baseline\"}");
}

bool jelli_profile_command(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    if (count < 4u || strcmp(words[2], "display") || strcmp(words[3], "profile"))
        return false;
    if (count == 6u && !strcmp(words[4], "overlays")) {
        overlay_mode(debug, id, words[5]);
        return true;
    }
    if (count == 6u && !strcmp(words[4], "memcpy")) {
        copy_mode(debug, id, words[5]);
        return true;
    }
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

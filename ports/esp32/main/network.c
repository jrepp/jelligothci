#include "network.h"
#include "network_internal.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/* Startup-only queues + worker stack. The engine thread owns all other state. */
static JelliNetworkWorker worker;
static JelliNetworkStatus state;
static JelliNetworkConfig draft;
static uint32_t submitted;
static uint64_t last_sync, reboot_after;
static bool ready, reboot_requested, reboot_blocked, boot_checked;
static char current_zone[96];

void jelli_network_init(const JelliPetEngine *engine)
{
    if (!jelli_network_load(&draft))
        return;
    state.active = draft;
    worker.state = state;
    if (engine->game.wall_known) {
        struct timeval tv = {.tv_sec = (time_t)engine->game.wall_seconds};
        (void)settimeofday(&tv, NULL);
    }
    worker.requests = xQueueCreate(1, sizeof(JelliNetworkRequest));
    worker.status = xQueueCreate(1, sizeof(JelliNetworkStatus));
    worker.time = xQueueCreate(1, sizeof(JelliNetworkTime));
    worker.events = xEventGroupCreate();
    if (worker.requests && worker.status && worker.time && worker.events)
        ready =
            xTaskCreate(jelli_network_worker, "jelli_network", 8192, &worker, 3, NULL) == pdPASS;
    if (!ready) {
        if (worker.requests)
            vQueueDelete(worker.requests);
        if (worker.status)
            vQueueDelete(worker.status);
        if (worker.time)
            vQueueDelete(worker.time);
        if (worker.events)
            vEventGroupDelete(worker.events);
    }
}

static int16_t offset_at(uint64_t seconds, int16_t fallback)
{
    const char *rule = jelli_network_timezone(state.active.timezone);
    if (!rule || !rule[0])
        return fallback;
    if (strcmp(current_zone, state.active.timezone)) {
        if (setenv("TZ", rule, 1) != 0)
            return fallback;
        tzset();
        memcpy(current_zone, state.active.timezone, sizeof(current_zone));
    }
    time_t utc_seconds = (time_t)seconds;
    struct tm local, utc;
    if (!localtime_r(&utc_seconds, &local) || !gmtime_r(&utc_seconds, &utc))
        return fallback;
    return jelli_network_offset(&local, &utc, fallback);
}

static void check_boot(const JelliPetEngine *engine, const JelliEspSession *session)
{
    if (boot_checked || !engine->ui.rendered)
        return;
    boot_checked = true;
    esp_ota_img_states_t image_state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &image_state) == ESP_OK &&
        image_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (session->storage_ready && !session->protected_save)
            (void)esp_ota_mark_app_valid_cancel_rollback();
        else
            (void)esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

void jelli_network_poll(JelliPetEngine *engine, JelliEspSession *session, bool frozen)
{
    check_boot(engine, session);
    if (!ready)
        return;
    (void)xQueueReceive(worker.status, &state, 0);
    if (frozen)
        return;
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    JelliNetworkTime sample;
    if (!engine->ui.wall_set_requested && xQueueReceive(worker.time, &sample, 0) == pdTRUE &&
        now >= sample.received_ms) {
        uint64_t seconds = sample.seconds + (now - sample.received_ms) / 1000u;
        if (seconds < UINT64_C(4102444800)) {
            engine->ui.wall_set_seconds = seconds;
            engine->ui.wall_set_offset_minutes = offset_at(seconds, engine->ui.timezone_minutes);
            engine->ui.wall_set_requested = true;
            last_sync = sample.seconds;
        }
    }
    if (engine->game.wall_known) {
        int16_t offset = offset_at(engine->game.wall_seconds, engine->ui.timezone_minutes);
        if (offset != engine->ui.timezone_minutes) {
            engine->ui.timezone_minutes = offset;
            engine->ui.save_requested = true;
        }
    }
    if (reboot_requested && now >= reboot_after) {
        reboot_requested = false;
        if (jelli_esp_session_checkpoint(session, engine))
            esp_restart();
        reboot_blocked = true;
    }
}

static void status_reply(JelliDebug *debug, uint32_t id)
{
    static const char *const names[] = {"idle", "downloading", "staged", "failed"};
    char body[512];
    int size = snprintf(
        body, sizeof(body),
        "{\"ok\":true,\"available\":%s,\"enabled\":%s,\"connected\":%s,"
        "\"pending\":%s,\"operation\":%" PRIu32 ",\"completed\":%" PRIu32 ","
        "\"result\":%d,\"ota\":\"%s\",\"last_ntp_seconds\":%" PRIu64 ","
        "\"timezone\":\"%s\",\"reboot_blocked\":%s,\"wifi_debug\":false,"
        "\"free_internal_bytes\":%" PRIu32 ",\"minimum_internal_bytes\":%" PRIu32 ","
        "\"worker_stack_free_bytes\":%" PRIu32 "}",
        ready ? "true" : "false", state.active.enabled ? "true" : "false",
        state.connected ? "true" : "false", submitted != state.completed ? "true" : "false",
        submitted, state.completed, (int)state.result, names[state.ota], last_sync,
        state.active.timezone, reboot_blocked ? "true" : "false", state.free_internal,
        state.minimum_internal, state.stack_free);
    if (size > 0 && (size_t)size < sizeof(body))
        jelli_debug_response(debug, id, body);
}

static void hex(const char *value, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i = 0;
    for (; value[i]; ++i) {
        unsigned ch = (unsigned char)value[i];
        out[i * 2u] = digits[ch >> 4u];
        out[i * 2u + 1u] = digits[ch & 15u];
    }
    out[i * 2u] = '\0';
}

static void settings_reply(JelliDebug *debug, uint32_t id)
{
    /* Engine-thread scratch: avoid a 1472-byte addition to the main task stack. */
    static char ssid[65], url[383], body[1024];
    hex(draft.ssid, ssid);
    hex(draft.ota_url, url);
    int size = snprintf(
        body, sizeof(body),
        "{\"ok\":true,\"scope\":\"device\",\"staged\":true,\"values\":{"
        "\"wifi.enabled\":%u,\"wifi.ssid_hex\":\"%s\",\"wifi.password_set\":%s,"
        "\"time.server\":\"%s\",\"time.timezone\":\"%s\",\"time.sync_seconds\":%" PRIu32 ","
        "\"ota.url_hex\":\"%s\"}}",
        draft.enabled ? 1u : 0u, ssid, draft.password[0] ? "true" : "false", draft.ntp,
        draft.timezone, draft.sync_seconds, url);
    if (size > 0 && (size_t)size < sizeof(body))
        jelli_debug_response(debug, id, body);
}

static void queue_request(JelliDebug *debug, uint32_t id, JelliNetworkOperation operation)
{
    if (submitted != state.completed || reboot_requested || state.ota == JELLI_OTA_STAGED) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"network_busy\"}");
        return;
    }
    if (operation == JELLI_NET_APPLY && !jelli_network_config_valid(&draft)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"invalid_network_config\"}");
        return;
    }
    JelliNetworkRequest request = {.config = draft, .id = submitted + 1u, .operation = operation};
    if (xQueueSend(worker.requests, &request, 0) == pdTRUE) {
        submitted = request.id;
        jelli_debug_response(debug, id, "{\"ok\":true,\"pending\":true}");
    } else {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"network_busy\"}");
    }
    memset(&request, 0, sizeof(request));
}

static void mutate(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    if (!strcmp(words[2], "tune") && count == 6u) {
        char value[192];
        bool ok = jelli_network_unhex(words[5], value, sizeof(value)) &&
                  jelli_network_config_set(&draft, words[4], value);
        memset(value, 0, sizeof(value));
        jelli_debug_response(debug, id,
                             ok ? "{\"ok\":true,\"staged\":true}"
                                : "{\"ok\":false,\"error\":\"device_tunable_range\"}");
    } else if (!strcmp(words[2], "network") && count == 4u && !strcmp(words[3], "apply")) {
        queue_request(debug, id, JELLI_NET_APPLY);
    } else if (!strcmp(words[2], "ota") && count == 4u && !strcmp(words[3], "start")) {
        queue_request(debug, id, JELLI_NET_OTA);
    } else if (!strcmp(words[2], "ota") && count == 4u && !strcmp(words[3], "reboot") &&
               state.ota == JELLI_OTA_STAGED && submitted == state.completed) {
        reboot_requested = true;
        reboot_blocked = false;
        reboot_after = (uint64_t)esp_timer_get_time() / 1000u + 250u;
        jelli_debug_response(debug, id, "{\"ok\":true,\"pending\":true}");
    } else {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax_or_not_staged\"}");
    }
}

bool jelli_network_command(void *ctx, JelliDebug *debug, const JelliPetEngine *engine, uint32_t id,
                           char **words, unsigned count)
{
    (void)ctx;
    bool settings = count >= 4u && !strcmp(words[3], "device") &&
                    (!strcmp(words[2], "tunables") || !strcmp(words[2], "tune"));
    if (!settings && strcmp(words[2], "network") && strcmp(words[2], "ota"))
        return false;
    if ((!strcmp(words[2], "network") || !strcmp(words[2], "ota")) && count == 3u)
        status_reply(debug, id);
    else if (!strcmp(words[2], "tunables") && count == 4u)
        settings_reply(debug, id);
    else if (!ready)
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"network_unavailable\"}");
    else if (debug->captured || engine->game.resuming)
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
    else
        mutate(debug, id, words, count);
    return true;
}

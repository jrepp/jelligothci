#include "power_history.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "nvs.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* NVS is shared with saves. Reserve space for the game and disable logging on
 * low space or an ambiguous write; never erase an existing namespace. */
enum { RESERVED_NVS_ENTRIES = 256 };
static nvs_handle_t storage;
static JelliPowerRecord records[JELLI_POWER_LOG_CAPACITY];
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t latest;
static int storage_error;
static bool ready;
/* Debug commands are serialized on the engine thread; keep reply scratch off its stack. */
static char reply_body[512];

static void key_for(unsigned slot, char key[5]) { (void)snprintf(key, 5, "p%02u", slot); }

static void set_error(esp_err_t error)
{
    portENTER_CRITICAL(&lock);
    ready = false;
    storage_error = (int)error;
    portEXIT_CRITICAL(&lock);
}

void jelli_power_history_init(void)
{
    esp_err_t result = nvs_open("powerlog", NVS_READWRITE, &storage);
    if (result != ESP_OK) {
        set_error(result);
        return;
    }
    for (unsigned i = 0u; i < JELLI_POWER_LOG_CAPACITY; ++i) {
        char key[5];
        uint8_t bytes[JELLI_POWER_LOG_RECORD_BYTES];
        size_t size = sizeof(bytes);
        key_for(i, key);
        result = nvs_get_blob(storage, key, bytes, &size);
        if (result == ESP_ERR_NVS_NOT_FOUND)
            continue;
        JelliPowerRecord record;
        if (result != ESP_OK || !jelli_power_record_decode(&record, bytes, size) ||
            record.sequence % JELLI_POWER_LOG_CAPACITY != i) {
            set_error(result == ESP_OK ? ESP_ERR_INVALID_RESPONSE : result);
            return;
        }
        portENTER_CRITICAL(&lock);
        records[i] = record;
        if (record.sequence > latest)
            latest = record.sequence;
        portEXIT_CRITICAL(&lock);
    }
    portENTER_CRITICAL(&lock);
    ready = true;
    portEXIT_CRITICAL(&lock);
}

void jelli_power_history_append(JelliPowerRecord record)
{
    if (!ready || latest == UINT32_MAX)
        return;
    nvs_stats_t stats;
    esp_err_t result = nvs_get_stats(NULL, &stats);
    if (result != ESP_OK || stats.available_entries < RESERVED_NVS_ENTRIES + 4u) {
        set_error(result == ESP_OK ? ESP_ERR_NVS_NOT_ENOUGH_SPACE : result);
        return;
    }
    record.sequence = latest + 1u;
    uint8_t bytes[JELLI_POWER_LOG_RECORD_BYTES];
    if (!jelli_power_record_encode(&record, bytes, sizeof(bytes))) {
        set_error(ESP_ERR_INVALID_ARG);
        return;
    }
    unsigned slot = record.sequence % JELLI_POWER_LOG_CAPACITY;
    char key[5];
    key_for(slot, key);
    result = nvs_set_blob(storage, key, bytes, sizeof(bytes));
    if (result == ESP_OK)
        result = nvs_commit(storage);
    if (result != ESP_OK) {
        set_error(result);
        return;
    }
    portENTER_CRITICAL(&lock);
    records[slot] = record;
    latest = record.sequence;
    portEXIT_CRITICAL(&lock);
}

bool jelli_power_history_command(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    if (count < 3u || strcmp(words[2], "powerlog"))
        return false;
    uint32_t offset = 0;
    if (count > 4u || (count == 4u && !jelli_debug_number(words[3], &offset)) ||
        offset >= JELLI_POWER_LOG_CAPACITY) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"range\"}");
        return true;
    }
    JelliPowerRecord record = {0};
    uint32_t last;
    bool available;
    int error;
    portENTER_CRITICAL(&lock);
    last = latest;
    available = ready;
    error = storage_error;
    if (count == 4u && last > offset) {
        uint32_t sequence = last - offset;
        JelliPowerRecord candidate = records[sequence % JELLI_POWER_LOG_CAPACITY];
        if (candidate.sequence == sequence)
            record = candidate;
    }
    portEXIT_CRITICAL(&lock);
    char *body = reply_body;
    const size_t capacity = sizeof(reply_body);
    int size;
    if (count == 3u)
        size = snprintf(body, capacity,
                        "{\"ok\":true,\"schema\":1,\"ready\":%s,\"error\":%d,\"latest\":%" PRIu32
                        ",\"capacity\":%u}",
                        available ? "true" : "false", error, last, JELLI_POWER_LOG_CAPACITY);
    else if (!record.sequence)
        size = snprintf(body, capacity, "{\"ok\":false,\"error\":\"not_found\"}");
    else
        size = snprintf(body, capacity,
                        "{\"ok\":true,\"schema\":1,\"seq\":%" PRIu32 ",\"boot\":%" PRIu32
                        ",\"ms\":%" PRIu64 ",\"wall\":%" PRIu64
                        ",\"mv\":%u,\"percent\":%u,\"status\":[%u,%u],\"flags\":%u,"
                        "\"reasons\":%u,\"reset\":%u}",
                        record.sequence, record.boot, record.monotonic_ms, record.wall_seconds,
                        record.battery_mv, record.battery_percent, record.status[0],
                        record.status[1], record.flags, record.reasons, record.reset_reason);
    if (size > 0 && (size_t)size < capacity)
        jelli_debug_response(debug, id, body);
    else
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"report\"}");
    return true;
}

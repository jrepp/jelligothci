#include "../ports/esp32/main/power_history.h"
#include "power_history_stubs/nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint8_t slots[JELLI_POWER_LOG_CAPACITY][JELLI_POWER_LOG_RECORD_BYTES];
static bool occupied[JELLI_POWER_LOG_CAPACITY];
static size_t available = 500u;
static unsigned commits;
static bool fail_commit;
static char response[512];
static unsigned slot(const char *key)
{
    CHECK(key && key[0] == 'p');
    char *end;
    unsigned long value = strtoul(key + 1, &end, 10);
    CHECK(end != key + 1 && !*end && value < JELLI_POWER_LOG_CAPACITY);
    return (unsigned)value;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    CHECK(!strcmp(name, "powerlog") && mode == NVS_READWRITE);
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length)
{
    CHECK(handle == 1);
    unsigned i = slot(key);
    if (!occupied[i])
        return ESP_ERR_NVS_NOT_FOUND;
    CHECK(*length >= sizeof(slots[i]));
    memcpy(value, slots[i], sizeof(slots[i]));
    *length = sizeof(slots[i]);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    CHECK(handle == 1 && length == JELLI_POWER_LOG_RECORD_BYTES);
    unsigned i = slot(key);
    memcpy(slots[i], value, length);
    occupied[i] = true;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    CHECK(handle == 1);
    ++commits;
    return fail_commit ? ESP_ERR_NVS_NOT_ENOUGH_SPACE : ESP_OK;
}
esp_err_t nvs_get_stats(const char *partition, nvs_stats_t *stats)
{
    CHECK(!partition);
    stats->available_entries = available;
    return ESP_OK;
}
void jelli_debug_response(JelliDebug *debug, uint32_t id, const char *body)
{
    (void)debug;
    (void)id;
    CHECK(strlen(body) < sizeof(response));
    (void)snprintf(response, sizeof(response), "%s", body);
}
bool jelli_debug_number(const char *text, uint32_t *out)
{
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (!*text || *end || value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}
static void status(void)
{
    char prefix[] = "@J1", id[] = "1", command[] = "powerlog";
    char *words[] = {prefix, id, command};
    CHECK(jelli_power_history_command(NULL, 1u, words, 3u));
}
static void query(unsigned offset)
{
    char prefix[] = "@J1", id[] = "1", command[] = "powerlog", index[4];
    (void)snprintf(index, sizeof(index), "%u", offset);
    char *words[] = {prefix, id, command, index};
    CHECK(jelli_power_history_command(NULL, 1u, words, 4u));
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    if (!strcmp(argv[1], "corrupt")) {
        occupied[0] = true;
        jelli_power_history_init();
        status();
        CHECK(strstr(response, "\"ready\":false") && !commits);
        return 0;
    }
    jelli_power_history_init();
    JelliPowerRecord record = {.boot = 42u, .reasons = JELLI_POWER_BOOT};
    if (!strcmp(argv[1], "low")) {
        available = 259u;
        jelli_power_history_append(record);
        status();
        CHECK(strstr(response, "\"ready\":false") && !commits);
        return 0;
    }
    if (!strcmp(argv[1], "commit_failure")) {
        fail_commit = true;
        jelli_power_history_append(record);
        status();
        CHECK(strstr(response, "\"ready\":false") && commits == 1 && occupied[1]);
        jelli_power_history_append(record);
        CHECK(commits == 1);
        return 0;
    }
    CHECK(!strcmp(argv[1], "ring"));
    for (unsigned i = 0u; i < JELLI_POWER_LOG_CAPACITY + 1u; ++i) {
        record.monotonic_ms = i;
        jelli_power_history_append(record);
    }
    CHECK(commits == JELLI_POWER_LOG_CAPACITY + 1u);
    status();
    CHECK(strstr(response, "\"latest\":33"));
    query(0u);
    CHECK(strstr(response, "\"seq\":33"));
    query(31u);
    CHECK(strstr(response, "\"seq\":2"));
    query(32u);
    CHECK(strstr(response, "\"error\":\"range\""));
    JelliPowerRecord decoded = {0};
    CHECK(jelli_power_record_decode(&decoded, slots[1], sizeof(slots[1])) &&
          decoded.sequence == 33u);
    return 0;
}

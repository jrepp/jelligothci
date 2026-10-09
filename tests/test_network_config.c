#include "../ports/esp32/main/network_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void persistence(void)
{
    JelliNetworkConfig config, decoded;
    jelli_network_defaults(&config);
    CHECK(jelli_network_config_valid(&config));
    CHECK(!config.enabled && !strcmp(config.timezone, "manual"));
    CHECK(jelli_network_config_set(&config, "wifi.ssid", "Home network"));
    CHECK(jelli_network_config_set(&config, "wifi.password", "eight chars"));
    CHECK(jelli_network_config_set(&config, "wifi.enabled", "1"));
    CHECK(jelli_network_config_set(&config, "time.timezone", "America/New_York"));
    CHECK(jelli_network_config_set(&config, "ota.url", "https://example.com/pet.bin"));
    CHECK(jelli_network_config_valid(&config));
    uint8_t bytes[JELLI_NETWORK_CONFIG_BYTES];
    jelli_network_config_encode(&config, bytes);
    CHECK(jelli_network_config_decode(&decoded, bytes, sizeof(bytes)));
    CHECK(!strcmp(decoded.password, config.password) && decoded.enabled);
    CHECK(!strcmp(decoded.ota_url, config.ota_url));
    CHECK(decoded.sync_seconds == 3600u);
    bytes[0] = 2u;
    CHECK(!jelli_network_config_decode(&decoded, bytes, sizeof(bytes)));
    CHECK(!strcmp(decoded.password, config.password));
    bytes[0] = 1u;
    CHECK(!jelli_network_config_decode(&decoded, bytes, sizeof(bytes) - 1u));
    memset(bytes + 6u, 'x', 33u);
    CHECK(!jelli_network_config_decode(&decoded, bytes, sizeof(bytes)));
}

static void validation(void)
{
    JelliNetworkConfig config;
    jelli_network_defaults(&config);
    CHECK(!jelli_network_config_set(&config, "wifi.enabled", "2"));
    CHECK(!jelli_network_config_set(&config, "time.sync_seconds", "59"));
    CHECK(!jelli_network_config_set(&config, "time.sync_seconds", "86401"));
    CHECK(!jelli_network_config_set(&config, "time.sync_seconds", "42949672960"));
    CHECK(!jelli_network_config_set(&config, "time.sync_seconds", "-1"));
    CHECK(jelli_network_config_set(&config, "time.sync_seconds", "86400"));
    CHECK(!jelli_network_config_set(&config, "time.server", "a\"b"));
    CHECK(!jelli_network_config_set(&config, "time.timezone", "made/up"));
    CHECK(!jelli_network_config_set(&config, "wifi.ssid", "12345678901234567890123456789012345"));
    CHECK(jelli_network_config_set(&config, "wifi.enabled", "1"));
    CHECK(!jelli_network_config_valid(&config));
    CHECK(jelli_network_config_set(&config, "wifi.ssid", "Home"));
    CHECK(!jelli_network_config_valid(&config));
    CHECK(jelli_network_config_set(&config, "wifi.password", "password"));
    CHECK(jelli_network_config_valid(&config));
    CHECK(jelli_network_config_set(&config, "ota.url", "http://example.com/app.bin"));
    CHECK(!jelli_network_config_valid(&config));
    CHECK(jelli_network_config_set(&config, "ota.url", "https://user:secret@example.com/app.bin"));
    CHECK(!jelli_network_config_valid(&config));
}

static void wire_strings(void)
{
    char out[5];
    CHECK(jelli_network_unhex("612062", out, sizeof(out)) && !strcmp(out, "a b"));
    CHECK(jelli_network_unhex("22205c", out, sizeof(out)) && !strcmp(out, "\" \\"));
    CHECK(jelli_network_unhex("-", out, sizeof(out)) && !out[0]);
    CHECK(!jelli_network_unhex("-", out, 0u));
    CHECK(!jelli_network_unhex("6161616161", out, sizeof(out)));
    CHECK(!jelli_network_unhex("0a", out, sizeof(out)));
    CHECK(!jelli_network_unhex("00", out, sizeof(out)));
    CHECK(!jelli_network_unhex("ff", out, sizeof(out)));
    CHECK(!jelli_network_unhex("xyz", out, sizeof(out)));
    CHECK(!jelli_network_unhex("gg", out, sizeof(out)));
}

static void offsets(void)
{
    struct tm utc = {.tm_yday = 0, .tm_hour = 1};
    struct tm local = {.tm_yday = 364, .tm_hour = 20};
    CHECK(jelli_network_offset(&local, &utc, 0) == -300);
    utc = (struct tm){.tm_yday = 365, .tm_hour = 20};
    local = (struct tm){.tm_yday = 0, .tm_hour = 1, .tm_min = 30};
    CHECK(jelli_network_offset(&local, &utc, 0) == 330);
    utc = (struct tm){.tm_yday = 100, .tm_hour = 3};
    local = (struct tm){.tm_yday = 99, .tm_hour = 23};
    CHECK(jelli_network_offset(&local, &utc, 0) == -240);
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        const char *rule = jelli_network_timezone(argv[1]);
        if (!rule)
            return 1;
        puts(rule);
        return 0;
    }
    offsets();
    persistence();
    validation();
    wire_strings();
    puts("Network config: bounds, persistence, credentials, zones and protocol encoding passed.");
    return 0;
}

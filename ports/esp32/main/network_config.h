#ifndef JELLI_NETWORK_CONFIG_H
#define JELLI_NETWORK_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

enum { JELLI_NETWORK_CONFIG_BYTES = 456 };
typedef struct {
    char ssid[33], password[65], ntp[64], timezone[96], ota_url[192];
    uint32_t sync_seconds;
    bool enabled;
} JelliNetworkConfig;

void jelli_network_defaults(JelliNetworkConfig *config);
/* Returns NULL for an unsupported region; empty rule means manual offset. */
const char *jelli_network_timezone(const char *region);
bool jelli_network_config_set(JelliNetworkConfig *config, const char *key, const char *value);
bool jelli_network_config_valid(const JelliNetworkConfig *config);
void jelli_network_config_encode(const JelliNetworkConfig *config,
                                 uint8_t bytes[JELLI_NETWORK_CONFIG_BYTES]);
bool jelli_network_config_decode(JelliNetworkConfig *config, const uint8_t *bytes, size_t size);
int16_t jelli_network_offset(const struct tm *local, const struct tm *utc, int16_t fallback);
bool jelli_network_unhex(const char *text, char *out, size_t capacity);
#endif

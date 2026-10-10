#include "wifi.h"
#include "wifi_internal.h"
#include "esp_wifi.h"
#include <string.h>

/* Settings' list of networks nearby (ADR-016), under wifi.c's lock. */

enum { SCAN_RECORDS = 24 };

/* A scan's raw records: static, as they are too big for a task's stack. Lock held. */
static wifi_ap_record_t records[SCAN_RECORDS];

static uint8_t security_of(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:
    case WIFI_AUTH_OWE:
        return JELLI_PREFS_OPEN_NETWORK;
    case WIFI_AUTH_WPA2_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:
        return JELLI_PREFS_ENTERPRISE_NETWORK;
    default:
        return JELLI_PREFS_PASSWORD_NETWORK;
    }
}

int jelli_agent_wifi_scan(JelliPrefsNetwork *out, size_t capacity)
{
    if (!out || !jelli_agent_wifi_take())
        return -1;
    wifi_scan_config_t config = {.show_hidden = false};
    uint16_t count = SCAN_RECORDS;
    bool ok = esp_wifi_scan_start(&config, true) == ESP_OK &&
              esp_wifi_scan_get_ap_records(&count, records) == ESP_OK;
    if (!ok)
        (void)esp_wifi_clear_ap_list();
    size_t written = 0u;
    for (uint16_t i = 0u; ok && i < count && written < capacity; ++i) {
        size_t size = strnlen((const char *)records[i].ssid, 32u);
        if (!size)
            continue;
        JelliPrefsNetwork *network = &out[written++];
        memset(network, 0, sizeof(*network));
        memcpy(network->ssid, records[i].ssid, size);
        network->ssid_size = (uint8_t)size;
        network->rssi = records[i].rssi;
        network->security = security_of(records[i].authmode);
    }
    jelli_agent_wifi_give();
    return ok ? (int)written : -1;
}

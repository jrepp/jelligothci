#include "wifi.h"
#include "settings.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Either option would make the driver open the default nvs partition, the pet's
 * save (ADR-013). A stale generated sdkconfig keeps them on, so refuse it. */
#if defined(CONFIG_ESP_WIFI_NVS_ENABLED) || defined(CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE)
#error "Wi-Fi would use the pet's nvs: delete the generated sdkconfig and rebuild"
#endif

enum { CONNECTED = 1 << 0, RETRIES = 5 };

static const char *TAG = "agent_wifi";
static EventGroupHandle_t events;
static StaticEventGroup_t events_storage;
static esp_netif_t *station;
static bool started;
/* Shared by the event task (decrements) and the console task (resets). */
static atomic_uint retries_left;

static void on_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, CONNECTED);
        /* A few quick retries, then wait for "wifi set" so a wrong password cannot
         * keep the radio busy. */
        unsigned left = atomic_load(&retries_left);
        if (left && atomic_compare_exchange_strong(&retries_left, &left, left - 1u))
            (void)esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&retries_left, RETRIES);
        xEventGroupSetBits(events, CONNECTED);
    }
}

/* Loads stored credentials into the driver and connects; false when none are stored. */
static bool connect_stored(void)
{
    wifi_config_t config = {0};
    size_t ssid_size = sizeof(config.sta.ssid), password_size = sizeof(config.sta.password);
    if (!jelli_agent_settings_get("ssid", config.sta.ssid, &ssid_size) || !ssid_size)
        return false;
    if (!jelli_agent_settings_get("pass", config.sta.password, &password_size))
        password_size = 0u;
    config.sta.threshold.authmode = password_size ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    config.sta.pmf_cfg.capable = true;
    (void)esp_wifi_disconnect();
    bool ok = esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK;
    memset(&config, 0, sizeof(config)); /* The driver keeps its own copy. */
    atomic_store(&retries_left, RETRIES);
    return ok && esp_wifi_connect() == ESP_OK;
}

bool jelli_agent_wifi_start(void)
{
    events = xEventGroupCreateStatic(&events_storage);
    esp_err_t loop = esp_event_loop_create_default();
    if (esp_netif_init() != ESP_OK || (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE))
        return false;
    station = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (!station || esp_wifi_init(&init) != ESP_OK ||
        esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK)
        return false;
    started = true;
    jelli_agent_wifi_power_save(true);
    if (!connect_stored())
        ESP_LOGI(TAG, "No Wi-Fi credentials yet; use scripts/jelli-agent wifi set");
    return true;
}

bool jelli_agent_wifi_set(const uint8_t *ssid, size_t ssid_size, const uint8_t *password,
                          size_t password_size)
{
    if (!started || !ssid_size || ssid_size > 32u ||
        (password_size && (password_size < 8u || password_size > 63u)))
        return false;
    bool ok = jelli_agent_settings_set("ssid", ssid, ssid_size) &&
              (password_size ? jelli_agent_settings_set("pass", password, password_size)
                             : jelli_agent_settings_erase("pass"));
    return ok && connect_stored();
}

bool jelli_agent_wifi_clear(void)
{
    if (started)
        (void)esp_wifi_disconnect();
    return jelli_agent_settings_erase("ssid") && jelli_agent_settings_erase("pass");
}

bool jelli_agent_wifi_wait(uint32_t ms)
{
    return started && (xEventGroupWaitBits(events, CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(ms)) &
                       CONNECTED) != 0;
}

void jelli_agent_wifi_power_save(bool on)
{
    if (started)
        (void)esp_wifi_set_ps(on ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

void jelli_agent_wifi_status(char *out, size_t capacity)
{
    uint8_t ssid[33];
    size_t ssid_size = 32u;
    bool provisioned = jelli_agent_settings_get("ssid", ssid, &ssid_size) && ssid_size;
    char ssid_hex[65] = "";
    for (size_t i = 0u; provisioned && i < ssid_size; ++i)
        (void)snprintf(ssid_hex + 2u * i, 3u, "%02x", ssid[i]);
    bool connected = jelli_agent_wifi_wait(0u);
    wifi_ap_record_t ap = {0};
    esp_netif_ip_info_t ip = {0};
    if (connected &&
        (esp_wifi_sta_get_ap_info(&ap) != ESP_OK || esp_netif_get_ip_info(station, &ip) != ESP_OK))
        connected = false;
    char address[16] = "0.0.0.0";
    if (connected)
        (void)esp_ip4addr_ntoa(&ip.ip, address, (int)sizeof(address));
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"provisioned\":%s,\"connected\":%s,\"ssid\":\"%s\","
                   "\"ip\":\"%s\",\"rssi\":%d,\"channel\":%u}",
                   provisioned ? "true" : "false", connected ? "true" : "false", ssid_hex, address,
                   connected ? ap.rssi : 0, connected ? ap.primary : 0u);
}

#include "wifi.h"
#include "settings.h"
#include "wifi_internal.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <stdio.h>

/* What the board's Wi-Fi is: the stored network, for Settings and the console. */

/* Without the lock: a join holds it for seconds and the UI task must not wait, and
 * NVS reads are safe beside writes. */
void jelli_agent_wifi_current(uint8_t *ssid, size_t *ssid_size, bool *secured, bool *connected)
{
    size_t size = 32u, password_size = 0u;
    if (!jelli_agent_wifi_started() || !jelli_agent_settings_get("ssid", ssid, &size))
        size = 0u;
    /* Its length only: NVS reports a blob's size when given no buffer. */
    *secured = size && jelli_agent_settings_get("pass", NULL, &password_size) && password_size;
    *ssid_size = size;
    *connected = jelli_agent_wifi_wait(0u);
}

void jelli_agent_wifi_status(char *out, size_t capacity)
{
    uint8_t ssid[33];
    size_t ssid_size = 0u;
    bool connected = false, secured = false;
    if (jelli_agent_wifi_started())
        jelli_agent_wifi_current(ssid, &ssid_size, &secured, &connected);
    char ssid_hex[65] = "";
    for (size_t i = 0u; i < ssid_size; ++i)
        (void)snprintf(ssid_hex + 2u * i, 3u, "%02x", ssid[i]);
    wifi_ap_record_t ap = {0};
    esp_netif_ip_info_t ip = {0};
    if (connected && (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
                      esp_netif_get_ip_info(jelli_agent_wifi_station(), &ip) != ESP_OK))
        connected = false;
    char address[16] = "0.0.0.0";
    if (connected)
        (void)esp_ip4addr_ntoa(&ip.ip, address, (int)sizeof(address));
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"provisioned\":%s,\"connected\":%s,\"ssid\":\"%s\","
                   "\"ip\":\"%s\",\"rssi\":%d,\"channel\":%u}",
                   ssid_size ? "true" : "false", connected ? "true" : "false", ssid_hex, address,
                   connected ? ap.rssi : 0, connected ? ap.primary : 0u);
}

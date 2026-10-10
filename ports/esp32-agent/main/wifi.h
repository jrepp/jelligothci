#ifndef JELLI_AGENT_WIFI_H
#define JELLI_AGENT_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Station-mode Wi-Fi for the voice agent (RFC-003). Credentials live in agent_nvs
 * (settings.h); the driver keeps its own copy in RAM only, so the pet's nvs is never
 * touched. Call from the console task. */
bool jelli_agent_wifi_start(void);
/* Stores credentials (SSID 1..32 bytes, password 0 or 8..63 bytes) and reconnects.
 * The password is wiped from this function's copies afterwards. */
bool jelli_agent_wifi_set(const uint8_t *ssid, size_t ssid_size, const uint8_t *password,
                          size_t password_size);
bool jelli_agent_wifi_clear(void);
/* Waits up to ms for an IPv4 address; true when connected. */
bool jelli_agent_wifi_wait(uint32_t ms);
/* Full power while streaming audio, modem sleep otherwise. */
void jelli_agent_wifi_power_save(bool on);
/* JSON object: provisioned, connected, ssid (hex), ip, rssi, channel. Never the password. */
void jelli_agent_wifi_status(char *out, size_t capacity);

#endif

#ifndef JELLI_AGENT_WIFI_H
#define JELLI_AGENT_WIFI_H

#include "jelli/agent_prefs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Station-mode Wi-Fi for the voice agent (RFC-003). Credentials live in agent_nvs
 * (settings.h); the driver keeps its own copy in RAM only, so the pet's nvs is never
 * touched. Start from the main task; after that the console and Settings' Wi-Fi task
 * may call the rest, which a lock takes one at a time. */
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

/* Settings (ADR-016). The networks nearby, hidden ones left out: a blocking scan of a
 * few seconds. Returns how many it wrote to out, or -1 when the scan failed. */
int jelli_agent_wifi_scan(JelliPrefsNetwork *out, size_t capacity);
/* Tries a network, waiting up to about 15 s. When it connects its credentials are
 * stored (as "wifi set" stores them); otherwise the stored network is reconnected, so
 * a mistyped password never costs the working one. The caller wipes the password. */
JelliPrefsOutcome jelli_agent_wifi_join(const uint8_t *ssid, size_t ssid_size, const char *password,
                                        size_t password_size);
/* The stored network's name (*ssid_size 0 when none; capacity at least 32), whether
 * it has a password, and whether the board is online. Never waits for the lock. */
void jelli_agent_wifi_current(uint8_t *ssid, size_t *ssid_size, bool *secured, bool *connected);

#endif

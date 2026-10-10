#ifndef JELLI_AGENT_SETTINGS_H
#define JELLI_AGENT_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

/* The agent's own settings in the agent_nvs partition (ADR-013); the pet's save in
 * nvs at 0x9000 is never opened. Values are blobs under short keys ("ssid", "pass",
 * "host", "port"). Open from the main task at start-up; then each call opens its own
 * NVS handle and NVS orders them, so any task may read. Writes that belong together
 * (a network's name and password) are ordered by their owner (wifi.c's lock). */
bool jelli_agent_settings_open(void);
/* On entry *size is the buffer capacity; on success it is the stored length. With out
 * NULL, only the stored length is read. */
bool jelli_agent_settings_get(const char *key, void *out, size_t *size);
bool jelli_agent_settings_set(const char *key, const void *data, size_t size);
/* Removes a key; a key that was never stored counts as removed. */
bool jelli_agent_settings_erase(const char *key);

#endif

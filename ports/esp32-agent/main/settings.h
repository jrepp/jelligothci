#ifndef JELLI_AGENT_SETTINGS_H
#define JELLI_AGENT_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

/* The agent's own settings in the agent_nvs partition (ADR-013); the pet's save in
 * nvs at 0x9000 is never opened. Values are blobs under short keys ("ssid", "pass",
 * "host", "port"). Not thread-safe: call from the console task only. */
bool jelli_agent_settings_open(void);
/* On entry *size is the buffer capacity; on success it is the stored length. */
bool jelli_agent_settings_get(const char *key, void *out, size_t *size);
bool jelli_agent_settings_set(const char *key, const void *data, size_t size);
/* Removes a key; a key that was never stored counts as removed. */
bool jelli_agent_settings_erase(const char *key);

#endif

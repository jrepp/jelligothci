#ifndef JELLI_AGENT_COMMANDS_SPIKE_H
#define JELLI_AGENT_COMMANDS_SPIKE_H

#include "jelli/agent_console.h"
#include <stdbool.h>
#include <stddef.h>

/* Console commands for the phase 0 audio and Wi-Fi spike (RFC-003):
 *   wifi set SSID_HEX [PASSWORD_HEX] | wifi clear | wifi status
 *   link set IPV4 PORT | link status        the Mac running tools/agent/spike_server.py
 *   mic stream SECONDS                      stream to the Mac, playing what it sends back
 *   audio test MS | audio volume 0..100     local tone with capture levels; speaker level
 * Returns false when the verb is not one of these, leaving out untouched. */
bool jelli_agent_command_spike(const JelliAgentRequest *request, char *out, size_t capacity);

/* Starts Wi-Fi, audio, and the link; a failure leaves the console and probes working. */
void jelli_agent_spike_start(void);

#endif

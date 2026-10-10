#ifndef JELLI_AGENT_COMMANDS_SETUP_H
#define JELLI_AGENT_COMMANDS_SETUP_H

#include "jelli/agent_console.h"
#include <stdbool.h>
#include <stddef.h>

/* Console commands for setting up the board (RFC-003):
 *   wifi set SSID_HEX [PASSWORD_HEX] | wifi clear | wifi status
 *   audio test MS | audio volume 0..100     local tone with capture levels; speaker level
 * Returns false when the verb is not one of these, leaving out untouched. */
bool jelli_agent_command_setup(const JelliAgentRequest *request, char *out, size_t capacity);

/* Opens the settings and starts Wi-Fi and audio; a failure leaves the console and
 * probes working. */
void jelli_agent_setup_start(void);

/* Whether audio started. */
bool jelli_agent_setup_audio_ready(void);
/* True when Wi-Fi started, so the radio feeds the hardware random number generator. */
bool jelli_agent_setup_wifi_ready(void);

#endif

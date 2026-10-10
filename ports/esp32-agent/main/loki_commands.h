#ifndef JELLI_AGENT_LOKI_COMMANDS_H
#define JELLI_AGENT_LOKI_COMMANDS_H

#include "jelli/agent_console.h"
#include <stdbool.h>
#include <stddef.h>

/* Console commands for trying the loki/1 link before the board's UI exists:
 *   loki status             pairing and link state
 *   loki inbox              the next message from the bridge, in canonical form
 *   loki answer CHOICE      answer the last question the inbox showed
 *   loki ptt start|stop     hold to talk: stream MIC1 to the bridge, then end
 *   loki cancel             send cancel
 *   loki models             the models the bridge offers as the default (ADR-018)
 *   loki model NAME         make NAME, one it offers, the default (set_model)
 * Returns false for other verbs. Console task only. */
bool jelli_agent_command_loki(const JelliAgentRequest *request, char *out, size_t capacity);

/* Starts the board's identity, pairing, player, microphone, and link after Wi-Fi
 * has started; without audio the link runs without the player and microphone, and
 * without the radio (Wi-Fi did not start) no new TLS identity is made. */
void jelli_agent_loki_start(bool audio, bool radio);

#endif

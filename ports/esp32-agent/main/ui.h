#ifndef JELLI_AGENT_UI_H
#define JELLI_AGENT_UI_H

#include "jelli/agent_console.h"
#include "jelli/agent_loki.h"
#include <stdbool.h>
#include <stddef.h>

/* The agent's screen (RFC-003): the unicorn at full size, as large as fits the round
 * panel, breathing between its two idle poses and floating gently, so it never sits
 * still on the AMOLED. Now and then it prances across the screen and back, and it
 * celebrates when the bridge reports a success (core/agent/prance.c). Prepare
 * resamples the art once into PSRAM; call it after the screen has started and before
 * the boot is confirmed, so a failure in it rolls back. Start then runs a task on
 * core 1 that redraws only what moved; false when preparing failed. */
bool jelli_agent_ui_prepare(void);
bool jelli_agent_ui_start(void);

/* While held the UI draws nothing, so the console's render bench has the panel; on
 * release it redraws the whole screen. */
void jelli_agent_ui_hold(bool held);

/* The bridge's state for the unicorn (the link's state hook); never blocks. */
void jelli_agent_ui_state(JelliLokiState state);

/* Handles "ui prance wander|celebrate"; false for other verbs. Console task. */
bool jelli_agent_command_ui(const JelliAgentRequest *request, char *out, size_t capacity);

#endif

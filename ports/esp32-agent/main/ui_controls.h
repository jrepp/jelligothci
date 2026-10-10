#ifndef JELLI_AGENT_UI_CONTROLS_H
#define JELLI_AGENT_UI_CONTROLS_H

#include "jelli/agent_console.h"
#include "jelli/agent_loki.h"
#include "jelli/engine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The agent's controls (ADR-016): the side button opens and closes Settings, touches
 * drive it, and the screen sleeps after two idle minutes until a double tap, the
 * button, or a moment the bridge needs the user wakes it. */

typedef enum { JELLI_UI_UNICORN, JELLI_UI_SETTINGS, JELLI_UI_DARK } JelliUiView;

enum { JELLI_AGENT_SCREEN_IDLE_MS = 120000 };

/* Startup, main task, after Wi-Fi and audio: touch, the button, Settings' errands. */
bool jelli_agent_ui_controls_start(void);

/* Each tick of the UI task: reads the button, touches, and requests from other tasks;
 * sleeps or wakes the screen; runs Settings, drawing it into canvas and presenting it.
 * Returns what the screen shows; *fresh is set when the unicorn must be redrawn whole,
 * because something else covered it. */
JelliUiView jelli_agent_ui_controls_tick(JelliSurface *canvas, uint64_t now_ms, bool *fresh);

/* Any task: the bridge's moments that need the user (listening, a question, a spoken
 * reply) wake the screen and keep it on. Never blocks. */
void jelli_agent_ui_controls_state(JelliLokiState state);

/* Console task: "ui button", "ui tap X Y", "ui drag up|down PX", and
 * "screen sleep|wake|status"; false for other verbs. */
bool jelli_agent_command_controls(const JelliAgentRequest *request, char *out, size_t capacity);

#endif

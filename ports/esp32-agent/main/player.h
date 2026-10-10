#ifndef JELLI_AGENT_PLAYER_H
#define JELLI_AGENT_PLAYER_H

#include "loki_link.h"
#include <stdbool.h>

/* Plays Loki Bridge's spoken replies (docs/loki-protocol.md). Reply frames arrive on
 * the WebSocket task through the link's hooks, wait in a PSRAM queue, and play after
 * a 300 ms lead; when a reply has played out, or was stopped, the bridge hears
 * "played". Frames of any other reply are dropped as they come up. */
bool jelli_agent_player_start(void);

/* The hooks to pass to jelli_agent_link_start. */
const JelliAgentPlayerHooks *jelli_agent_player_hooks(void);

/* Stops the current reply at once, as stop_audio does; holding to talk calls it,
 * since the board never listens and speaks at the same time. */
void jelli_agent_player_stop(void);

/* True while a reply is playing or waiting for its lead. */
bool jelli_agent_player_busy(void);

#endif

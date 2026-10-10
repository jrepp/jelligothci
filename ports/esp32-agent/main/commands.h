#ifndef JELLI_AGENT_COMMANDS_H
#define JELLI_AGENT_COMMANDS_H

#include "jelli/agent_console.h"
#include <stddef.h>

/* Runs one console request and writes its JSON reply body into out:
 *   hello                      app, protocol, board, and version
 *   probe chip|heap|pmu        read-only snapshots
 *   probe pwr MS | probe te MS sample the PWR key or count panel TE pulses
 *   boot pet                   arm a restart into the pet after this reply
 * and the commands in commands_setup.h, pairing.h, loki_commands.h, and
 * render_bench.h. */
void jelli_agent_command(const JelliAgentRequest *request, char *out, size_t capacity);

#endif

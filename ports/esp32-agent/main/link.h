#ifndef JELLI_AGENT_LINK_H
#define JELLI_AGENT_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Phase 0 audio spike link (RFC-003): one WebSocket session to
 * tools/agent/spike_server.py on the Mac, LAN only and unencrypted; phase 1 replaces
 * it with jelli-agent/1 over mutual TLS. During a session the board streams 60 ms
 * microphone frames (see jelli_agent_frame_header) and plays any mono PCM the Mac
 * sends between "play_begin" and "play_end" messages. */

/* Creates the playback buffer and task once at startup. */
bool jelli_agent_link_init(void);

/* Streams for 1..30 seconds to ws://host:port/agent, then waits for queued playback
 * to finish and writes a JSON summary into out. Blocks the calling task. */
void jelli_agent_link_stream(const char *host, uint16_t port, uint32_t seconds, char *out,
                             size_t capacity);

#endif

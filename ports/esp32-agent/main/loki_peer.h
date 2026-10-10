#ifndef JELLI_AGENT_LOKI_PEER_H
#define JELLI_AGENT_LOKI_PEER_H

#include "pairing.h"
#include <stdbool.h>
#include <stdint.h>

/* Internal to the link (loki_link.c): where the paired bridge is, and the one socket
 * that reaches it. */

/* The bridge's IPv4 address, as dotted text in dial: its name through DNS or mDNS
 * when that answers (names ending in .local can fail now and then), else the last
 * address that resolved for this pairing generation, else the one the bridge gave at
 * pairing. Link task only. */
bool jelli_agent_peer_resolve(const JelliAgentBridge *bridge, uint32_t generation, char dial[16]);

/* Records the address and port a session dials, so an abort can find its socket;
 * false when dial is not a dotted IPv4 address. Clear between sessions. Link task
 * only, while no session is running. */
bool jelli_agent_peer_set(const char *dial, uint16_t port);
void jelli_agent_peer_clear(void);

/* Shuts down the socket connected to the recorded peer, if there is one, so a read,
 * write, or TLS handshake blocked on it fails at once. Any task. */
void jelli_agent_peer_shutdown(void);

#endif

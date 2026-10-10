#ifndef JELLI_AGENT_PAIRING_H
#define JELLI_AGENT_PAIRING_H

#include "jelli/agent_console.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Which Loki Bridge this board trusts (docs/loki-protocol.md): the bridge's
 * certificate, which the TLS client accepts as its only trust anchor, and where to
 * reach it. Set over the USB console by the bridge's pairing, which also reads this
 * board's certificate fingerprint:
 *   pair show                       this board's name and certificate fingerprint
 *   pair begin                      start receiving a bridge certificate
 *   pair cert OFFSET HEX            the next part of its DER, at most 96 bytes
 *   pair commit HOST PORT SHA256 [IPV4]   check the whole certificate and store it all
 *   pair status | pair clear
 * HOST is a dotted IPv4 address or a name such as "studio.local"; IPV4, the Mac's
 * address when it paired, is tried when the name does not resolve. */

typedef struct {
    uint8_t cert[1024];
    size_t cert_size;
    char host[64];
    char address[16]; /* Fallback IPv4, or empty. */
    uint16_t port;
} JelliAgentBridge;

/* Loads the stored pairing at startup, after the settings are open. */
bool jelli_agent_pairing_start(void);

/* Copies the stored pairing; false when the board is not paired. Thread-safe. */
bool jelli_agent_pairing_get(JelliAgentBridge *bridge);

/* Counts changes, so the link notices a new or removed pairing. */
uint32_t jelli_agent_pairing_generation(void);

/* Handles "pair ..."; false for other verbs. Console task only. */
bool jelli_agent_command_pair(const JelliAgentRequest *request, char *out, size_t capacity);

#endif

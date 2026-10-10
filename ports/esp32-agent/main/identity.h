#ifndef JELLI_AGENT_IDENTITY_H
#define JELLI_AGENT_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The board's TLS identity for loki/1 (docs/loki-protocol.md): an ECDSA P-256 key
 * and a self-signed certificate made on the board the first time it starts, so the
 * private key never leaves it. Both live in agent_nvs, unencrypted (RFC-003: flash
 * encryption is a separate, irreversible decision). The bridge pins the
 * certificate's SHA-256, which pairing over USB hands it. */

/* Loads the identity, or makes and stores one. A new key is made only when radio_on
 * (Wi-Fi has started), since without the radio's noise the hardware random number
 * generator is only pseudo-random; until then there is no identity and no link. */
bool jelli_agent_identity_start(bool radio_on);

/* DER encodings for the TLS client; valid after a successful start. */
const uint8_t *jelli_agent_identity_cert(size_t *size);
const uint8_t *jelli_agent_identity_key(size_t *size);

/* Lowercase hex SHA-256 of the certificate (65 bytes with the NUL), and the board's
 * name, "Loki Agent" and the last three bytes of its Wi-Fi MAC. */
const char *jelli_agent_identity_fingerprint(void);
const char *jelli_agent_identity_name(void);

#endif

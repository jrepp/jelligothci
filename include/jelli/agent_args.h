#ifndef JELLI_AGENT_ARGS_H
#define JELLI_AGENT_ARGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Strict parsers for agent console arguments (RFC-003 phase 0). Each validates the
 * whole word and writes its output only on success. */

/* Hex with an even number of digits (either case) into at most capacity bytes. An
 * empty word decodes to zero bytes. Used for Wi-Fi credentials, which may contain
 * spaces and so cannot be console words themselves. */
bool jelli_agent_hex_decode(const char *text, uint8_t *out, size_t capacity, size_t *size);

/* Dotted-quad IPv4 such as 192.168.1.20: four decimal parts of 0..255 without signs,
 * spaces, or leading zeros. */
bool jelli_agent_ipv4_parse(const char *text, uint8_t address[4]);

/* Where the board finds Loki Bridge: a dotted IPv4 address, or a DNS name of at most
 * 63 characters made of letters, digits, and dashes in dot-separated labels that are
 * not empty and neither start nor end with a dash, the last starting with a letter
 * (such as "studio.local"; RFC 1123), so that no name reads as a numeric address. */
bool jelli_agent_host_valid(const char *host);

/* Header of each microphone frame sent to the Mac: "JA", version 1, channel count,
 * then the frame sequence and the capture time in milliseconds, both u32 LE.
 * Interleaved int16 LE samples follow (tools/agent/spike_server.py reads them). */
#define JELLI_AGENT_FRAME_HEADER 12u
void jelli_agent_frame_header(uint8_t out[JELLI_AGENT_FRAME_HEADER], uint8_t channels,
                              uint32_t sequence, uint32_t capture_ms);

#endif

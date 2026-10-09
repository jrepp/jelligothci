#ifndef JELLI_AGENT_PCM_H
#define JELLI_AGENT_PCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reassembles int16 PCM from network reads that may split a message at any byte
 * (RFC-003 phase 0 playback). An odd trailing byte waits for the next read of the
 * same message rather than shifting every later sample by one byte; the start of a
 * new message drops it, because a new message never continues an old sample.
 * Zero-initialize before the first read. Fixed size, no allocation. */
typedef struct {
    uint8_t carry;
    bool carrying;
} JelliAgentPcmJoin;

/* What one read yields, in order: an optional sample completed from the carried
 * byte, then an even-length run that points into the read's own data. */
typedef struct {
    uint8_t joined[2];
    size_t joined_size; /* 0 or 2 */
    const uint8_t *body;
    size_t body_size; /* even; 0 when the read held no complete sample */
} JelliAgentPcmParts;

/* message_start is true for the first read of a new message: payload offset 0 of a
 * frame that is not a continuation frame (WebSocket opcode 0x0 continues the
 * message, so it keeps the carried byte). data may be NULL only when size is 0.
 * parts is always fully written. */
void jelli_agent_pcm_join(JelliAgentPcmJoin *join, const uint8_t *data, size_t size,
                          bool message_start, JelliAgentPcmParts *parts);

#endif

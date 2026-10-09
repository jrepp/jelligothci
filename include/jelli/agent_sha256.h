#ifndef JELLI_AGENT_SHA256_H
#define JELLI_AGENT_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* SHA-256 (FIPS 180-4) for the Loki protocol's card digest. Portable and
 * allocation-free; the state is caller-owned. */
#define JELLI_SHA256_SIZE 32u

typedef struct {
    uint32_t h[8];
    uint64_t length; /* Bytes hashed so far. */
    uint8_t block[64];
    size_t used;
} JelliSha256;

void jelli_sha256_init(JelliSha256 *sha);
void jelli_sha256_update(JelliSha256 *sha, const void *data, size_t size);
void jelli_sha256_final(JelliSha256 *sha, uint8_t digest[JELLI_SHA256_SIZE]);

#endif

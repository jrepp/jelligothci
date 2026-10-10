#ifndef JELLI_SAVE_H
#define JELLI_SAVE_H

#include "jelli/game.h"
#include <stddef.h>

#define JELLI_SAVE_VERSION 5u
#define JELLI_SAVE_CAPACITY 4096u

typedef struct {
    JelliGame game;
    uint64_t sequence, anchor_ms;
    bool anchor_valid;
} JelliSave;

/* Explicit versioned little-endian codec; decode failure leaves output unchanged. */
size_t jelli_save_encode(const JelliSave *save, uint8_t *bytes, size_t capacity);
/* Convenience decoder places a full JelliSave on the calling stack. */
bool jelli_save_decode(JelliSave *save, const uint8_t *bytes, size_t size);

/* Caller-owned scratch avoids a full JelliSave on embedded task stacks.
 * Scratch must be distinct from output and input storage; scratch may change on failure. */
bool jelli_save_decode_workspace(JelliSave *save, const uint8_t *bytes, size_t size,
                                 JelliSave *scratch);

#endif

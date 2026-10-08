#ifndef JELLI_ASSETS_H
#define JELLI_ASSETS_H

#include <stdint.h>

typedef struct {
    uint32_t id;
    uint16_t width;
    uint16_t height;
    const uint16_t *pixels;
    const uint8_t *mask;
    uint8_t mask_stride;
} JelliAsset;

/* Generated from assets/slice/assets.json by tools/assets/embed_slice.py. */
const JelliAsset *jelli_asset_find(uint32_t id);
/* Font ID 4001 stores glyph-major 8x12 one-byte rows, codepoints 32..127. */
const uint8_t *jelli_asset_glyph(uint8_t codepoint);

#endif

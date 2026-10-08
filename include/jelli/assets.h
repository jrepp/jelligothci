#ifndef JELLI_ASSETS_H
#define JELLI_ASSETS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t id;
    uint16_t width;
    uint16_t height;
    const uint16_t *pixels;
    const uint8_t *mask;
    uint8_t mask_stride;
    /* Creature contact point, Q8 source-pixel edges. Zero for non-creatures.
     * X: centroid of bottom three opaque rows; Y: exclusive opaque bottom. */
    uint16_t ground_x_q8, ground_y_q8;
    uint16_t centroid_x_q8, centroid_y_q8;
    uint8_t left, top, right, bottom; /* Cached exclusive alpha bounds. */
} JelliAsset;

/* Optional host-owned artwork. Storage must stay valid throughout a frame.
 * NULL uses the immutable embedded assets (including on ESP32). */
typedef struct {
    const JelliAsset *items;
    size_t count;
    const uint8_t *glyphs; /* Exactly 96 * 12 bytes, or NULL for embedded font. */
} JelliAssetSet;
const JelliAsset *jelli_asset_lookup(const JelliAssetSet *set, uint32_t id);
const uint8_t *jelli_asset_lookup_glyph(const JelliAssetSet *set, uint8_t codepoint);

/* Generated from assets/slice/assets.json by tools/assets/embed_slice.py. */
const JelliAsset *jelli_asset_find(uint32_t id);
/* Font ID 4001 stores glyph-major 8x12 one-byte rows, codepoints 32..127. */
const uint8_t *jelli_asset_glyph(uint8_t codepoint);

#endif

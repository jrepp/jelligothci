#ifndef JELLI_ASSETS_H
#define JELLI_ASSETS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Largest supported density (stored pixels per logical pixel). */
#define JELLI_ASSET_MAX_DENSITY 8u
/* Stored alpha at or above this value counts as covered for bounds and hit tests. */
#define JELLI_ASSET_OPAQUE_ALPHA 128u

/* Immutable sprite descriptor. Layout fields (width/height, anchors, bounds) are
 * LOGICAL pixels, the unit every draw scale and screen layout uses. Pixel planes are
 * STORED at density D: (width * D) x (height * D), row-major. Legacy art has D = 1,
 * a 1-bit mask and no alpha plane; high-resolution art has D > 1 and an 8-bit
 * straight-alpha plane instead of a mask. */
typedef struct {
    uint32_t id;
    uint16_t width;
    uint16_t height;
    const uint16_t *pixels; /* Stored RGB565, native endian. */
    const uint8_t *mask;    /* Stored 1-bit coverage, MSB first; NULL when alpha is set. */
    uint8_t mask_stride;    /* Bytes per stored mask row. */
    /* Creature contact point, Q8 logical-pixel edges. Zero for non-creatures.
     * X: centroid of the bottom three logical rows of coverage; Y: exclusive bottom. */
    uint16_t ground_x_q8, ground_y_q8;
    uint16_t centroid_x_q8, centroid_y_q8;
    uint8_t left, top, right, bottom; /* Cached exclusive logical coverage bounds. */
    uint8_t density;                  /* Stored pixels per logical pixel; 0 or 1 is legacy. */
    const uint8_t *alpha;             /* Stored straight alpha; NULL uses the mask. */
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

/* Density 1..JELLI_ASSET_MAX_DENSITY (0 is legacy 1), or 0 for an unusable value. */
unsigned jelli_asset_density(const JelliAsset *asset);
/* True when logical `scale` is drawable: scale == D, a multiple of D (nearest
 * upscale) or a divisor of D (box downscale). */
bool jelli_asset_scale_supported(const JelliAsset *asset, unsigned scale);
/* Stored 8-bit coverage at stored pixel (x, y): the alpha plane, or 0/255 from the
 * mask. Zero outside the stored image or for an incomplete descriptor. */
uint8_t jelli_asset_alpha_at(const JelliAsset *asset, unsigned x, unsigned y);
/* Hit test for output pixel (x, y), relative to the sprite origin, of a sprite drawn
 * at logical `scale`: alpha of the stored pixel beneath it is at least
 * JELLI_ASSET_OPAQUE_ALPHA. False for unsupported scales and outside the sprite. */
bool jelli_asset_hit(const JelliAsset *asset, unsigned x, unsigned y, unsigned scale);

/* Reads a sprite at a logical scale, in output pixels. Each output pixel covers
 * `down` x `down` stored pixels (box filter) and each stored pixel covers `up` x
 * `up` output pixels (nearest); at most one of them exceeds 1. */
typedef struct {
    const JelliAsset *asset;
    unsigned up, down;      /* Output pixels per stored pixel; stored per output pixel. */
    unsigned stored_width;  /* Row pitch, in elements, of pixels and alpha. */
    unsigned width, height; /* Output size: logical size times scale. */
} JelliAssetSampler;
/* Prepares a sampler; false (sampler untouched) for incomplete descriptors, scales
 * outside 1..16 and unsupported scale/density pairs. */
bool jelli_asset_sampler(JelliAssetSampler *sampler, const JelliAsset *asset, unsigned scale);
/* Straight-alpha sample of output pixel (x, y); zero outside width x height.
 * Box downscales average premultiplied colour and alpha in integers. Returns alpha
 * 0..255; *color is the straight RGB565 colour and is only written when alpha > 0. */
uint8_t jelli_asset_sample(const JelliAssetSampler *sampler, unsigned x, unsigned y,
                           uint16_t *color);

/* Generated from assets/slice/assets.json by tools/assets/embed_slice.py. */
const JelliAsset *jelli_asset_find(uint32_t id);
/* Font ID 4001 stores glyph-major 8x12 one-byte rows, codepoints 32..127. */
const uint8_t *jelli_asset_glyph(uint8_t codepoint);

#endif

#ifndef JELLI_SDL_ASSET_RELOAD_H
#define JELLI_SDL_ASSET_RELOAD_H

#include "jelli/pet_engine.h"

/* Sized for the full high-resolution set (about 1.65 M stored pixels, 4.96 MB of
 * RGB565 + alpha) with headroom. Two banks plus the file buffer total about 20 MiB,
 * allocated once when --asset-pack is given. */
#define JELLI_ASSET_PACK_CAPACITY 8388608u
#define JELLI_ASSET_PACK_COUNT 128u
#define JELLI_ASSET_PACK_PIXELS 2097152u
#define JELLI_ASSET_PACK_MASK_BYTES 12288u

/* Pack v1 has 24-byte legacy records. v2 appends density, three zero bytes (28 bytes);
 * density 1 carries RGB565 + mask, density 2..8 carries stored RGB565 + 8-bit alpha. */
typedef struct {
    JelliAsset descriptors[JELLI_ASSET_PACK_COUNT];
    uint16_t pixels[JELLI_ASSET_PACK_PIXELS];
    uint8_t alpha[JELLI_ASSET_PACK_PIXELS];
    uint8_t masks[JELLI_ASSET_PACK_MASK_BYTES], glyphs[1152u];
    JelliAssetSet set;
} JelliAssetBank;

typedef struct {
    JelliAssetBank banks[2];
    uint8_t bytes[JELLI_ASSET_PACK_CAPACITY];
    const char *path;
    uint64_t last_poll_ms;
    uint32_t checksum, count;
    unsigned active;
    bool sampled, loaded, rejected;
} JelliAssetReload;

/* Desktop owns one bounded startup allocation; no frame-time allocation. */
JelliAssetReload *jelli_sdl_asset_reload_open(const char *path);
void jelli_sdl_asset_reload_poll(JelliAssetReload *reload, JelliPetEngine *engine, uint64_t now);
/* Validates and decodes a JLAP v1/v2 pack (header checksum is the caller's job). */
bool jelli_sdl_asset_pack_decode(JelliAssetBank *bank, const uint8_t *bytes, size_t size);

#endif

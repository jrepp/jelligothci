#ifndef JELLI_SDL_ASSET_RELOAD_H
#define JELLI_SDL_ASSET_RELOAD_H

#include "jelli/pet_engine.h"

#define JELLI_ASSET_PACK_CAPACITY 262144u
#define JELLI_ASSET_PACK_COUNT 128u

typedef struct {
    JelliAsset descriptors[JELLI_ASSET_PACK_COUNT];
    uint16_t pixels[98304u];
    uint8_t masks[12288u], glyphs[1152u];
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
bool jelli_sdl_asset_pack_decode(JelliAssetBank *bank, const uint8_t *bytes, size_t size);

#endif

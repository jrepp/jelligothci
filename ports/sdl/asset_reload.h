#ifndef JELLI_SDL_ASSET_RELOAD_H
#define JELLI_SDL_ASSET_RELOAD_H

#include "jelli/pet_engine.h"

/* 448 KiB staging buffer covers one bank's payload plus record metadata. */
#define JELLI_ASSET_PACK_CAPACITY 458752u
#define JELLI_ASSET_PACK_COUNT 192u

typedef struct {
    JelliAsset descriptors[JELLI_ASSET_PACK_COUNT];
    uint16_t pixels[196608u]; /* Room for 48x48 creature forms; see ADR-012. */
    uint8_t masks[24576u], glyphs[1152u];
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

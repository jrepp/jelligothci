#ifndef JELLI_SDL_ASSET_RELOAD_H
#define JELLI_SDL_ASSET_RELOAD_H

#include "jelli/pet_engine.h"

/* 576 KiB staging buffer covers one bank's payload plus record metadata. */
#define JELLI_ASSET_PACK_CAPACITY 589824u
#define JELLI_ASSET_PACK_COUNT 192u

typedef struct {
    JelliAsset descriptors[JELLI_ASSET_PACK_COUNT];
    uint16_t pixels[262144u]; /* Bounded studio artwork bank; includes layered scenes. */
    uint8_t masks[32768u], glyphs[1152u];
    JelliAssetSet set;
} JelliAssetBank;

/* Whole-bank reload storage remains the current implementation. Proposed
 * bounded workspace migration follows the memory foundations:
 * docs-cms/rfcs/rfc-009-bounded-memory-systems-and-adoption.md#adoption-sequence */
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

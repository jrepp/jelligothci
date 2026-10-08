#include "asset_reload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8u | (uint32_t)bytes[2] << 16u |
           (uint32_t)bytes[3] << 24u;
}

static uint32_t checksum(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    return ~crc;
}

JelliAssetReload *jelli_sdl_asset_reload_open(const char *path)
{
    if (!path)
        return NULL;
    JelliAssetReload *reload = calloc(1u, sizeof(*reload));
    if (reload)
        reload->path = path;
    return reload;
}

static bool load(JelliAssetReload *reload, JelliPetEngine *engine)
{
    FILE *file = fopen(reload->path, "rb");
    if (!file)
        return false;
    size_t size = fread(reload->bytes, 1u, sizeof(reload->bytes), file);
    bool valid = !ferror(file) && fgetc(file) == EOF;
    if (fclose(file) != 0)
        valid = false;
    if (!valid || size < 16u || memcmp(reload->bytes, "JLAP", 4u) ||
        u32(reload->bytes + 4u) != 1u || u32(reload->bytes + 8u) > JELLI_ASSET_PACK_COUNT)
        return false;
    uint32_t crc = u32(reload->bytes + 12u), count = u32(reload->bytes + 8u);
    if (crc != checksum(reload->bytes + 16u, size - 16u))
        return false;
    if (reload->loaded && crc == reload->checksum && count == reload->count)
        return true;
    unsigned next = reload->active ^ 1u;
    if (!jelli_sdl_asset_pack_decode(&reload->banks[next], reload->bytes, size))
        return false;
    reload->active = next;
    reload->checksum = crc;
    reload->count = count;
    reload->loaded = true;
    engine->ui.assets = &reload->banks[next].set;
    engine->ui.rendered = false;
    engine->ui.actor_frame = NULL;
    fprintf(stderr, "Live artwork applied: %u assets\n", (unsigned)count);
    return true;
}

void jelli_sdl_asset_reload_poll(JelliAssetReload *reload, JelliPetEngine *engine, uint64_t now)
{
    if (!reload ||
        (reload->sampled && now >= reload->last_poll_ms && now - reload->last_poll_ms < 500u))
        return;
    reload->sampled = true;
    reload->last_poll_ms = now;
    bool ok = load(reload, engine);
    if (!ok && !reload->rejected)
        fprintf(stderr, "Live artwork unavailable or invalid; keeping previous artwork\n");
    reload->rejected = !ok;
}

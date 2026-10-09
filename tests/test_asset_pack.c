#include "asset_reload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static JelliAssetBank bank;
static uint8_t pack[JELLI_ASSET_PACK_CAPACITY];

static size_t put16(size_t at, unsigned value)
{
    pack[at] = (uint8_t)(value & 0xffu);
    pack[at + 1u] = (uint8_t)(value >> 8u);
    return at + 2u;
}

static size_t put32(size_t at, uint32_t value)
{
    at = put16(at, value & 0xffffu);
    return put16(at, value >> 16u);
}

static size_t header(uint32_t version, uint32_t count)
{
    memcpy(pack, "JLAP", 4u);
    put32(4u, version);
    put32(8u, count);
    return put32(12u, 0u); /* The reload host checks the CRC; the decoder does not. */
}

/* Record for a fully covered square of `size` logical pixels at `density`. */
static size_t record(size_t at, uint32_t version, uint32_t id, unsigned size, unsigned density,
                     uint32_t length)
{
    unsigned center = size * 128u;
    at = put32(at, id);
    at = put16(at, size);
    at = put16(at, size);
    at = put16(at, 0u);
    at = put16(at, 0u);
    at = put16(at, center);
    at = put16(at, center);
    pack[at++] = 0u;
    pack[at++] = 0u;
    pack[at++] = (uint8_t)size;
    pack[at++] = (uint8_t)size;
    at = put32(at, length);
    if (version == 2u) {
        pack[at++] = (uint8_t)density;
        pack[at++] = 0u;
        pack[at++] = 0u;
        pack[at++] = 0u;
    }
    return at;
}

/* Opaque background 10001 (legacy, 64x64) as both pack versions. */
static size_t legacy_pack(uint32_t version, unsigned density)
{
    size_t at = record(header(version, 1u), version, 10001u, 64u, density, 4096u * 2u + 512u);
    for (unsigned i = 0u; i < 4096u; ++i)
        at = put16(at, 0x07e0u);
    memset(pack + at, 0xff, 512u);
    return at + 512u;
}

/* Opaque star 9001 at density 2: 32x32 stored RGB565 then 32x32 alpha. */
static size_t hires_pack(uint8_t corner_alpha)
{
    size_t at = record(header(2u, 1u), 2u, 9001u, 16u, 2u, 32u * 32u * 3u);
    for (unsigned i = 0u; i < 1024u; ++i)
        at = put16(at, 0xf81fu);
    memset(pack + at, 0xff, 1024u);
    pack[at] = corner_alpha;
    return at + 1024u;
}

static void synthetic_packs(void)
{
    CHECK(jelli_sdl_asset_pack_decode(&bank, pack, legacy_pack(1u, 1u)));
    CHECK(bank.descriptors[0].density == 1u && bank.descriptors[0].mask &&
          !bank.descriptors[0].alpha);
    CHECK(jelli_sdl_asset_pack_decode(&bank, pack, legacy_pack(2u, 1u)));
    CHECK(!jelli_sdl_asset_pack_decode(&bank, pack, legacy_pack(3u, 1u))); /* Unknown version. */
    size_t size = legacy_pack(2u, 1u);
    pack[16u + 25u] = 1u; /* Reserved bytes must be zero. */
    CHECK(!jelli_sdl_asset_pack_decode(&bank, pack, size));
    CHECK(!jelli_sdl_asset_pack_decode(&bank, pack, legacy_pack(2u, 0u)));
    CHECK(!jelli_sdl_asset_pack_decode(&bank, pack, legacy_pack(2u, 9u)));
    size = hires_pack(255u);
    CHECK(jelli_sdl_asset_pack_decode(&bank, pack, size));
    const JelliAsset *star = &bank.descriptors[0];
    CHECK(star->density == 2u && star->alpha && !star->mask && star->width == 16u);
    CHECK(star->pixels[1023] == 0xf81fu && star->alpha[0] == 255u);
    CHECK(jelli_asset_hit(star, 0u, 0u, 2u) && jelli_asset_hit(star, 31u, 31u, 2u));
    CHECK(!jelli_sdl_asset_pack_decode(&bank, pack, size - 1u)); /* Truncated payload. */
    /* A faint corner (alpha < 128) changes bounds and centroid: stale metadata fails. */
    CHECK(!jelli_sdl_asset_pack_decode(&bank, pack, hires_pack(127u)));
    /* Backgrounds stay legacy even when the payload size and geometry are right. */
    const size_t stored = (size_t)128u * 128u;
    size = record(header(2u, 1u), 2u, 10001u, 64u, 2u, (uint32_t)(stored * 3u));
    for (size_t i = 0u; i < stored; ++i)
        size = put16(size, 0x07e0u);
    memset(pack + size, 0xff, stored);
    CHECK(!jelli_sdl_asset_pack_decode(&bank, pack, size + stored));
}

/* Decodes a pack file produced by tools/assets/live_assets.py and prints one line per
 * asset: id, density and whether it carries an alpha plane. */
static int decode_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    size_t size = fread(pack, 1u, sizeof(pack), file);
    bool complete = !ferror(file) && size < sizeof(pack); /* Larger files are invalid. */
    CHECK(fclose(file) == 0 && complete);
    if (!jelli_sdl_asset_pack_decode(&bank, pack, size)) {
        fprintf(stderr, "decode failed: %s\n", path);
        return 1;
    }
    for (size_t i = 0u; i < bank.set.count; ++i) {
        const JelliAsset *a = &bank.descriptors[i];
        printf("%u %u %u\n", (unsigned)a->id, jelli_asset_density(a), (unsigned)(a->alpha != NULL));
    }
    return 0;
}

int main(int argc, char **argv)
{
    synthetic_packs();
    if (argc > 1)
        return decode_file(argv[1]);
    puts("PASS: pack v1/v2 decoding, density/alpha payloads, metadata and bounds checks");
    return 0;
}

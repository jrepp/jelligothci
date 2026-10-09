#include "asset_reload.h"
#include <string.h>

typedef struct {
    size_t pixels, alpha, masks;
} Cursor;

/* Stored coverage (alpha >= 128) totals; sums use 64 bits for 2040-pixel rows. */
typedef struct {
    uint64_t count, sum_x, sum_y, band_count, band_x;
    unsigned left, top, right, bottom;
} Coverage;

static uint16_t u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u));
}

static uint32_t u32(const uint8_t *bytes)
{
    return (uint32_t)u16(bytes) | (uint32_t)u16(bytes + 2u) << 16u;
}

static bool metadata(const JelliAsset *asset, const JelliAsset *original)
{
    return original && asset->width == original->width && asset->height == original->height &&
           asset->left < asset->right && asset->top < asset->bottom &&
           asset->right <= asset->width && asset->bottom <= asset->height &&
           asset->ground_x_q8 <= (uint32_t)asset->width * 256u &&
           asset->ground_y_q8 <= (uint32_t)asset->height * 256u &&
           asset->centroid_x_q8 <= (uint32_t)asset->width * 256u &&
           asset->centroid_y_q8 <= (uint32_t)asset->height * 256u;
}

/* Props (3xxx), the font (4001) and backgrounds (10xxx) are read at logical
 * resolution by the background, atmosphere and glyph paths; keep them legacy. */
static bool legacy_only(uint32_t id)
{
    uint32_t family = id / 1000u;
    return family == 3u || family == 4u || family == 10u;
}

static void measure(const JelliAsset *asset, unsigned density, Coverage *c)
{
    unsigned width = asset->width * density, height = asset->height * density;
    *c = (Coverage){.left = width, .top = height};
    for (unsigned y = 0u; y < height; ++y) {
        for (unsigned x = 0u; x < width; ++x) {
            if (jelli_asset_alpha_at(asset, x, y) < JELLI_ASSET_OPAQUE_ALPHA)
                continue;
            c->left = x < c->left ? x : c->left;
            c->top = y < c->top ? y : c->top;
            c->right = x + 1u > c->right ? x + 1u : c->right;
            c->bottom = y + 1u > c->bottom ? y + 1u : c->bottom;
            ++c->count;
            c->sum_x += x * 2u + 1u;
            c->sum_y += y * 2u + 1u;
        }
    }
    /* Contact band: the bottom three logical rows, i.e. 3 * density stored rows. */
    unsigned band = 3u * density;
    for (unsigned y = c->bottom > band ? c->bottom - band : 0u; y < c->bottom; ++y) {
        for (unsigned x = 0u; x < width; ++x) {
            if (jelli_asset_alpha_at(asset, x, y) >= JELLI_ASSET_OPAQUE_ALPHA) {
                ++c->band_count;
                c->band_x += x * 2u + 1u;
            }
        }
    }
}

/* Mean stored half-pixel coordinate as logical Q8, rounding half up; this matches
 * tools/assets/sprite_geometry.py exactly. count is nonzero. */
static uint64_t q8(uint64_t sum, uint64_t count, unsigned density)
{
    return (sum * 128u + count * density / 2u) / (count * density);
}

static bool geometry(const JelliAsset *asset, unsigned density)
{
    Coverage c;
    measure(asset, density, &c);
    if (!c.count || c.left / density != asset->left || c.top / density != asset->top ||
        (c.right + density - 1u) / density != asset->right ||
        (c.bottom + density - 1u) / density != asset->bottom ||
        q8(c.sum_x, c.count, density) != asset->centroid_x_q8 ||
        q8(c.sum_y, c.count, density) != asset->centroid_y_q8)
        return false;
    if (jelli_asset_find(asset->id)->ground_y_q8)
        return c.band_count && asset->ground_y_q8 == (c.bottom * 256u + density / 2u) / density &&
               asset->ground_x_q8 == q8(c.band_x, c.band_count, density);
    return asset->ground_x_q8 == 0u && asset->ground_y_q8 == 0u;
}

static bool payload_mask(JelliAssetBank *bank, JelliAsset *asset, const uint8_t *bytes, size_t size,
                         Cursor *at)
{
    size_t count = (size_t)asset->width * asset->height;
    size_t stride = ((size_t)asset->width + 7u) / 8u;
    size_t mask_size = stride * asset->height;
    if (size != count * 2u + mask_size || count > JELLI_ASSET_PACK_PIXELS - at->pixels ||
        mask_size > sizeof(bank->masks) - at->masks || stride > UINT8_MAX)
        return false;
    asset->pixels = bank->pixels + at->pixels;
    asset->mask = bank->masks + at->masks;
    asset->mask_stride = (uint8_t)stride;
    for (size_t i = 0u; i < count; ++i)
        bank->pixels[at->pixels + i] = u16(bytes + i * 2u);
    memcpy(bank->masks + at->masks, bytes + count * 2u, mask_size);
    at->pixels += count;
    at->masks += mask_size;
    return geometry(asset, 1u);
}

static bool payload_alpha(JelliAssetBank *bank, JelliAsset *asset, const uint8_t *bytes,
                          size_t size, Cursor *at)
{
    unsigned density = asset->density;
    size_t count = (size_t)asset->width * density * asset->height * density;
    if (legacy_only(asset->id) || size != count * 3u ||
        count > JELLI_ASSET_PACK_PIXELS - at->pixels || count > sizeof(bank->alpha) - at->alpha)
        return false;
    asset->pixels = bank->pixels + at->pixels;
    asset->alpha = bank->alpha + at->alpha;
    for (size_t i = 0u; i < count; ++i)
        bank->pixels[at->pixels + i] = u16(bytes + i * 2u);
    memcpy(bank->alpha + at->alpha, bytes + count * 2u, count);
    at->pixels += count;
    at->alpha += count;
    return geometry(asset, density);
}

static bool payload(JelliAssetBank *bank, JelliAsset *asset, const uint8_t *bytes, size_t size,
                    Cursor *at)
{
    if (asset->id == 4001u) {
        if (asset->density != 1u || size != sizeof(bank->glyphs))
            return false;
        memcpy(bank->glyphs, bytes, size);
        bank->set.glyphs = bank->glyphs;
        return true;
    }
    if (asset->density == 1u)
        return payload_mask(bank, asset, bytes, size, at);
    return payload_alpha(bank, asset, bytes, size, at);
}

/* Decodes one record header; v2 appends density and three reserved zero bytes. */
static bool record(JelliAsset *asset, const uint8_t *bytes, uint32_t version, uint32_t *length)
{
    *asset = (JelliAsset){.id = u32(bytes),
                          .width = u16(bytes + 4u),
                          .height = u16(bytes + 6u),
                          .ground_x_q8 = u16(bytes + 8u),
                          .ground_y_q8 = u16(bytes + 10u),
                          .centroid_x_q8 = u16(bytes + 12u),
                          .centroid_y_q8 = u16(bytes + 14u),
                          .left = bytes[16],
                          .top = bytes[17],
                          .right = bytes[18],
                          .bottom = bytes[19],
                          .density = version == 2u ? bytes[24] : 1u};
    *length = u32(bytes + 20u);
    if (version == 2u && (bytes[25] || bytes[26] || bytes[27]))
        return false;
    return asset->density >= 1u && asset->density <= JELLI_ASSET_MAX_DENSITY;
}

bool jelli_sdl_asset_pack_decode(JelliAssetBank *bank, const uint8_t *bytes, size_t size)
{
    if (!bank || !bytes || size < 16u || size > JELLI_ASSET_PACK_CAPACITY ||
        memcmp(bytes, "JLAP", 4u))
        return false;
    uint32_t version = u32(bytes + 4u), count = u32(bytes + 8u);
    size_t record_size = version == 2u ? 28u : 24u;
    if ((version != 1u && version != 2u) || !count || count > JELLI_ASSET_PACK_COUNT)
        return false;
    bank->set = (JelliAssetSet){.items = bank->descriptors, .count = count};
    size_t at = 16u;
    Cursor cursor = {0u, 0u, 0u};
    for (unsigned i = 0u; i < count; ++i) {
        if (size - at < record_size)
            return false;
        JelliAsset *asset = &bank->descriptors[i];
        uint32_t length;
        if (!record(asset, bytes + at, version, &length))
            return false;
        at += record_size;
        if (length > size - at || !metadata(asset, jelli_asset_find(asset->id)))
            return false;
        for (unsigned previous = 0u; previous < i; ++previous) {
            if (bank->descriptors[previous].id == asset->id)
                return false;
        }
        if (!payload(bank, asset, bytes + at, length, &cursor))
            return false;
        at += length;
    }
    return at == size;
}

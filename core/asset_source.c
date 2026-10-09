#include "jelli/assets.h"

/* Largest logical draw scale accepted by the sampler; the renderer uses 1..6. */
#define MAX_SCALE 16u

const JelliAsset *jelli_asset_lookup(const JelliAssetSet *set, uint32_t id)
{
    if (set && set->items)
        for (size_t i = 0u; i < set->count; ++i)
            if (set->items[i].id == id)
                return &set->items[i];
    return jelli_asset_find(id);
}

const uint8_t *jelli_asset_lookup_glyph(const JelliAssetSet *set, uint8_t codepoint)
{
    if (!set || !set->glyphs)
        return jelli_asset_glyph(codepoint);
    if (codepoint < 32u || codepoint > 127u)
        codepoint = (uint8_t)'?';
    return &set->glyphs[(size_t)(codepoint - 32u) * 12u];
}

unsigned jelli_asset_density(const JelliAsset *asset)
{
    if (!asset)
        return 0u;
    if (asset->density <= 1u)
        return 1u;
    return asset->density <= JELLI_ASSET_MAX_DENSITY ? asset->density : 0u;
}

bool jelli_asset_scale_supported(const JelliAsset *asset, unsigned scale)
{
    unsigned density = jelli_asset_density(asset);
    return density && scale && scale <= MAX_SCALE &&
           (scale % density == 0u || density % scale == 0u);
}

/* A descriptor is drawable when its stored planes exist and the mask covers a row. */
static bool readable(const JelliAsset *asset, unsigned density)
{
    unsigned width = (unsigned)asset->width * density;
    return asset->pixels && asset->width && asset->height &&
           (asset->alpha || (asset->mask && asset->mask_stride >= (width + 7u) / 8u));
}

/* Unchecked stored coverage; callers have validated the descriptor and position. */
static uint8_t texel_alpha(const JelliAsset *asset, unsigned pitch, unsigned x, unsigned y)
{
    if (asset->alpha)
        return asset->alpha[(size_t)y * pitch + x];
    return (asset->mask[(size_t)y * asset->mask_stride + x / 8u] & (0x80u >> (x % 8u))) ? 255u : 0u;
}

uint8_t jelli_asset_alpha_at(const JelliAsset *asset, unsigned x, unsigned y)
{
    unsigned density = jelli_asset_density(asset);
    if (!density || !readable(asset, density) || x >= (unsigned)asset->width * density ||
        y >= (unsigned)asset->height * density)
        return 0u;
    return texel_alpha(asset, (unsigned)asset->width * density, x, y);
}

bool jelli_asset_sampler(JelliAssetSampler *sampler, const JelliAsset *asset, unsigned scale)
{
    if (!sampler || !jelli_asset_scale_supported(asset, scale))
        return false;
    unsigned density = jelli_asset_density(asset);
    if (!readable(asset, density))
        return false;
    *sampler = (JelliAssetSampler){.asset = asset,
                                   .up = scale >= density ? scale / density : 1u,
                                   .down = scale < density ? density / scale : 1u,
                                   .stored_width = (unsigned)asset->width * density,
                                   .width = (unsigned)asset->width * scale,
                                   .height = (unsigned)asset->height * scale};
    return true;
}

bool jelli_asset_hit(const JelliAsset *asset, unsigned x, unsigned y, unsigned scale)
{
    JelliAssetSampler sampler;
    if (!jelli_asset_sampler(&sampler, asset, scale) || x >= sampler.width || y >= sampler.height)
        return false;
    unsigned column = x / sampler.up * sampler.down, row = y / sampler.up * sampler.down;
    return jelli_asset_alpha_at(asset, column, row) >= JELLI_ASSET_OPAQUE_ALPHA;
}

/* Premultiplied k x k box: colour channels are weighted by alpha, so transparent
 * texels never tint the edge. Sums fit easily: k <= 8, 64 * 63 * 255 < 2^20. */
static uint8_t box(const JelliAssetSampler *s, unsigned left, unsigned top, uint16_t *color)
{
    unsigned alpha = 0u, red = 0u, green = 0u, blue = 0u, k = s->down;
    for (unsigned row = top; row < top + k; ++row) {
        for (unsigned column = left; column < left + k; ++column) {
            unsigned a = texel_alpha(s->asset, s->stored_width, column, row);
            uint16_t texel = s->asset->pixels[(size_t)row * s->stored_width + column];
            alpha += a;
            red += ((texel >> 11) & 31u) * a;
            green += ((texel >> 5) & 63u) * a;
            blue += (texel & 31u) * a;
        }
    }
    unsigned count = k * k, average = (alpha + count / 2u) / count;
    if (!alpha || !average)
        return 0u;
    unsigned r = (red + alpha / 2u) / alpha, g = (green + alpha / 2u) / alpha;
    unsigned b = (blue + alpha / 2u) / alpha;
    *color = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint8_t)average;
}

uint8_t jelli_asset_sample(const JelliAssetSampler *sampler, unsigned x, unsigned y,
                           uint16_t *color)
{
    if (x >= sampler->width || y >= sampler->height)
        return 0u;
    unsigned column = x, row = y;
    if (sampler->up > 1u) {
        column /= sampler->up;
        row /= sampler->up;
    }
    if (sampler->down > 1u)
        return box(sampler, column * sampler->down, row * sampler->down, color);
    uint8_t alpha = texel_alpha(sampler->asset, sampler->stored_width, column, row);
    if (alpha)
        *color = sampler->asset->pixels[(size_t)row * sampler->stored_width + column];
    return alpha;
}

#include "asset_reference.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t ref_next(RefRandom *random)
{
    uint32_t x = random->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    random->state = x;
    return x;
}

unsigned ref_below(RefRandom *random, unsigned bound) { return ref_next(random) % bound; }

/* Every plane holds at least one stored pixel, so size is never zero. */
static void *allocate(size_t size)
{
    void *block = malloc(size);
    if (!block) {
        fputs("asset_reference: out of memory\n", stderr);
        exit(1);
    }
    return block;
}

void ref_fill_alpha(uint8_t *alpha, size_t count, RefRandom *random)
{
    static const uint8_t edges[] = {0u, 1u, 127u, 128u, 254u, 255u};
    unsigned mode = ref_below(random, 6u), mid = 1u + ref_below(random, 254u);
    for (size_t i = 0u; i < count; ++i) {
        unsigned roll = ref_below(random, 256u);
        switch (mode) {
        case 0u:
            alpha[i] = 0u;
            break;
        case 1u:
            alpha[i] = 255u;
            break;
        case 2u:
            alpha[i] = (uint8_t)mid;
            break;
        case 3u:
            alpha[i] = (uint8_t)roll;
            break;
        case 4u: /* Mostly binary with soft sprinkles, like real antialiased art. */
            alpha[i] = roll < 100u ? 0u : roll < 220u ? 255u : (uint8_t)ref_next(random);
            break;
        default:
            alpha[i] = edges[roll % sizeof(edges)];
            break;
        }
    }
}

void ref_sprite_alpha(RefSprite *sprite, uint32_t id, unsigned width, unsigned height,
                      unsigned density)
{
    size_t count = (size_t)width * density * height * density;
    *sprite = (RefSprite){0};
    sprite->count = count;
    sprite->pixels = allocate(count * sizeof(uint16_t));
    sprite->alpha = allocate(count);
    memset(sprite->pixels, 0, count * sizeof(uint16_t));
    memset(sprite->alpha, 0, count);
    sprite->asset = (JelliAsset){.id = id,
                                 .width = (uint16_t)width,
                                 .height = (uint16_t)height,
                                 .pixels = sprite->pixels,
                                 .right = (uint8_t)width,
                                 .bottom = (uint8_t)height,
                                 .density = (uint8_t)density,
                                 .alpha = sprite->alpha};
}

/* Breaks one part of a complete descriptor; the sampler must refuse it. */
static void break_descriptor(JelliAsset *asset, unsigned how)
{
    if (how == 0u)
        asset->pixels = NULL;
    else if (how == 1u)
        asset->width = 0u;
    else if (how == 2u)
        asset->height = 0u;
    else if (asset->mask)
        asset->mask = NULL;
    else
        asset->alpha = NULL; /* No alpha and no mask. */
}

void ref_sprite_random(RefSprite *sprite, RefRandom *random, uint32_t id, unsigned max_logical,
                       bool broken)
{
    static const uint8_t densities[] = {0u, 1u, 1u, 2u, 2u, 3u, 3u, 4u,  4u,
                                        5u, 6u, 6u, 7u, 8u, 8u, 9u, 255u};
    uint8_t raw = densities[ref_below(random, sizeof(densities))];
    unsigned density = raw <= 1u || raw > JELLI_ASSET_MAX_DENSITY ? 1u : raw;
    unsigned width = 1u + ref_below(random, max_logical);
    unsigned height = 1u + ref_below(random, max_logical);
    bool masked = raw > 1u ? ref_below(random, 5u) == 0u : ref_below(random, 5u) != 0u;
    ref_sprite_alpha(sprite, id, width, height, density);
    sprite->asset.density = raw;
    for (size_t i = 0u; i < sprite->count; ++i)
        sprite->pixels[i] = (uint16_t)ref_next(random);
    ref_fill_alpha(sprite->alpha, sprite->count, random);
    if (masked) {
        unsigned needed = (width * density + 7u) / 8u, stride = needed + ref_below(random, 3u);
        size_t size = (size_t)stride * height * density;
        free(sprite->alpha);
        sprite->alpha = NULL;
        sprite->mask = allocate(size);
        for (size_t i = 0u; i < size; ++i) /* Padding bits and bytes are random too. */
            sprite->mask[i] = (uint8_t)ref_next(random);
        sprite->asset.alpha = NULL;
        sprite->asset.mask = sprite->mask;
        sprite->asset.mask_stride = (uint8_t)stride;
        if (broken && ref_below(random, 12u) == 0u)
            sprite->asset.mask_stride = (uint8_t)(needed - 1u); /* One byte short a row. */
    }
    if (broken && ref_below(random, 10u) == 0u)
        break_descriptor(&sprite->asset, ref_below(random, 4u));
}

void ref_sprite_free(RefSprite *sprite)
{
    free(sprite->pixels);
    free(sprite->alpha);
    free(sprite->mask);
    *sprite = (RefSprite){0};
}

unsigned ref_density(const JelliAsset *asset)
{
    if (asset->density <= 1u)
        return 1u;
    return asset->density <= 8u ? asset->density : 0u;
}

bool ref_readable(const JelliAsset *asset)
{
    unsigned density = ref_density(asset);
    if (!density || !asset->pixels || !asset->width || !asset->height)
        return false;
    return asset->alpha != NULL ||
           (asset->mask != NULL && 8u * asset->mask_stride >= (unsigned)asset->width * density);
}

bool ref_supported(const JelliAsset *asset, unsigned scale)
{
    unsigned density = ref_density(asset);
    if (!density || scale < 1u || scale > 16u)
        return false;
    return scale % density == 0u || density % scale == 0u;
}

uint8_t ref_texel(const JelliAsset *asset, unsigned x, unsigned y)
{
    unsigned pitch = asset->width * ref_density(asset);
    if (asset->alpha)
        return asset->alpha[(size_t)y * pitch + x];
    uint8_t byte = asset->mask[(size_t)y * asset->mask_stride + x / 8u];
    return ((byte >> (7u - x % 8u)) & 1u) ? 255u : 0u;
}

unsigned ref_sample(const JelliAsset *asset, unsigned scale, unsigned x, unsigned y,
                    uint16_t *color)
{
    unsigned density = ref_density(asset), pitch = asset->width * density;
    if (!scale || !density)
        return 0u; /* Callers only sample drawable sprites. */
    unsigned n = density > scale ? density / scale : 1u;
    /* Output pixel x covers stored columns [x * D / s, x * D / s + n): exact either way
     * because s divides D or D divides s. */
    unsigned left = x * density / scale, top = y * density / scale;
    uint64_t sum[4] = {0u, 0u, 0u, 0u};
    for (unsigned row = top; row < top + n; ++row)
        for (unsigned column = left; column < left + n; ++column) {
            unsigned a = ref_texel(asset, column, row);
            uint16_t texel = asset->pixels[(size_t)row * pitch + column];
            sum[0] += a;
            sum[1] += (uint64_t)(texel >> 11) * a;
            sum[2] += (uint64_t)((texel >> 5) & 63u) * a;
            sum[3] += (uint64_t)(texel & 31u) * a;
        }
    uint64_t cells = (uint64_t)n * n, alpha = (sum[0] + cells / 2u) / cells;
    if (!alpha || !sum[0])
        return 0u;
    uint64_t r = (sum[1] + sum[0] / 2u) / sum[0], g = (sum[2] + sum[0] / 2u) / sum[0];
    uint64_t b = (sum[3] + sum[0] / 2u) / sum[0];
    *color = (uint16_t)(r << 11 | g << 5 | b);
    return (unsigned)alpha;
}

uint16_t ref_blend(uint16_t back, uint16_t front, unsigned alpha)
{
    static const unsigned shifts[3] = {11u, 5u, 0u}, masks[3] = {31u, 63u, 31u};
    if (alpha >= 255u)
        return front;
    unsigned out = 0u;
    for (unsigned c = 0u; c < 3u; ++c) {
        unsigned b = (back >> shifts[c]) & masks[c], f = (front >> shifts[c]) & masks[c];
        out |= ((f * alpha + b * (255u - alpha) + 127u) / 255u) << shifts[c];
    }
    return (uint16_t)out;
}

bool ref_in_round(int x, int y)
{
    /* (x + 0.5 - 233)^2 + (y + 0.5 - 233)^2 <= 233^2, scaled by four. */
    long dx = 2L * x + 1L - 466L, dy = 2L * y + 1L - 466L;
    return dx * dx + dy * dy <= 466L * 466L;
}

bool ref_bounds_match(const JelliAsset *asset)
{
    unsigned d = ref_density(asset), left = 0xffffu, top = 0xffffu, right = 0u, bottom = 0u;
    for (unsigned y = 0u; y < asset->height * d; ++y)
        for (unsigned x = 0u; x < asset->width * d; ++x)
            if (ref_texel(asset, x, y) >= JELLI_ASSET_OPAQUE_ALPHA) {
                left = x < left ? x : left;
                top = y < top ? y : top;
                right = x + 1u > right ? x + 1u : right;
                bottom = y + 1u > bottom ? y + 1u : bottom;
            }
    return right && asset->left == left / d && asset->top == top / d &&
           asset->right == (right + d - 1u) / d && asset->bottom == (bottom + d - 1u) / d;
}

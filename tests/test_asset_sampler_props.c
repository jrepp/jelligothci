/* Seeded property tests for the density sampler (core/asset_source.c). Random synthetic
 * sprites (logical 1..33 px, density field 0..9 and 255, an 8-bit alpha or 1-bit mask
 * plane with padded rows, clear/opaque/mid/uniform/near-binary alpha, and incomplete
 * descriptors) are sampled at every scale 0..17 and compared with the independent model
 * in asset_reference.c: the scale rule, refusal without touching the sampler, nearest
 * upscale, the premultiplied box filter, colour written only under coverage, stored
 * alpha lookups, and the hit test. Planes are exact-size heap blocks, so an
 * out-of-range read fails under AddressSanitizer. Metamorphic relations need no
 * oracle: a nearest-upscaled copy at density D samples exactly like its density-1
 * original at every scale D supports, and a 1-bit mask samples exactly like the
 * equivalent 0/255 alpha plane. A failure prints the seed and scale. */
#include "asset_reference.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_CASE(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x scale %u)\n", __FILE__, __LINE__, #expression, \
                    (unsigned)current_seed, current_scale);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SPRITES = 1500, MAX_LOGICAL = 33, FULL_SCAN = 24000, SPOT_CHECKS = 1500 };
static uint32_t current_seed;
static unsigned current_scale;

typedef struct {
    unsigned refused, drawable, samples, covered, soft, boxed, upscaled;
} Tally;

static void check_one(const JelliAssetSampler *sampler, const JelliAsset *asset, unsigned scale,
                      unsigned x, unsigned y, Tally *tally)
{
    uint16_t got_color = 0xbeefu, want_color = 0u;
    unsigned want = ref_sample(asset, scale, x, y, &want_color);
    unsigned got = jelli_asset_sample(sampler, x, y, &got_color);
    CHECK_CASE(got == want);
    CHECK_CASE(want ? got_color == want_color : got_color == 0xbeefu); /* Written only if seen. */
    unsigned density = ref_density(asset);
    bool hit = ref_texel(asset, x * density / scale, y * density / scale) >= 128u;
    CHECK_CASE(jelli_asset_hit(asset, x, y, scale) == hit);
    tally->samples++;
    tally->covered += want ? 1u : 0u;
    tally->soft += want && want < 255u ? 1u : 0u;
}

/* Outside the output nothing is read or written and nothing is hit. */
static void check_outside(const JelliAssetSampler *sampler, const JelliAsset *asset, unsigned scale)
{
    static const unsigned far = UINT_MAX;
    const unsigned points[][2] = {{sampler->width, 0u},       {0u, sampler->height},
                                  {sampler->width, far},      {far, far},
                                  {sampler->width - 1u, far}, {far, sampler->height - 1u}};
    for (size_t i = 0u; i < sizeof(points) / sizeof(points[0]); ++i) {
        uint16_t color = 0xbeefu;
        CHECK_CASE(jelli_asset_sample(sampler, points[i][0], points[i][1], &color) == 0u);
        CHECK_CASE(color == 0xbeefu);
        CHECK_CASE(!jelli_asset_hit(asset, points[i][0], points[i][1], scale));
    }
}

static void check_scale(const JelliAsset *asset, unsigned scale, RefRandom *random, Tally *tally)
{
    current_scale = scale;
    JelliAssetSampler sampler;
    unsigned char untouched[sizeof(sampler)];
    memset(&sampler, 0xcd, sizeof(sampler));
    memcpy(untouched, &sampler, sizeof(untouched));
    bool drawable = ref_supported(asset, scale) && ref_readable(asset);
    CHECK_CASE(jelli_asset_scale_supported(asset, scale) == ref_supported(asset, scale));
    CHECK_CASE(jelli_asset_sampler(&sampler, asset, scale) == drawable);
    if (!drawable) {
        unsigned char after[sizeof(sampler)];
        memcpy(after, &sampler, sizeof(after)); /* Raw bytes: refusal writes nothing. */
        CHECK_CASE(memcmp(untouched, after, sizeof(after)) == 0);
        CHECK_CASE(!jelli_asset_hit(asset, 0u, 0u, scale));
        tally->refused++;
        return;
    }
    unsigned density = ref_density(asset), width = asset->width * scale;
    unsigned height = asset->height * scale;
    CHECK_CASE(sampler.asset == asset && sampler.width == width && sampler.height == height);
    CHECK_CASE(sampler.stored_width == asset->width * density);
    CHECK_CASE(scale >= density ? sampler.up == scale / density && sampler.down == 1u
                                : sampler.up == 1u && sampler.down == density / scale);
    tally->drawable++;
    tally->boxed += sampler.down > 1u ? 1u : 0u;
    tally->upscaled += sampler.up > 1u ? 1u : 0u;
    if ((size_t)width * height <= FULL_SCAN) {
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x)
                check_one(&sampler, asset, scale, x, y, tally);
    } else {
        check_one(&sampler, asset, scale, width - 1u, height - 1u, tally); /* Last texel. */
        for (unsigned i = 0u; i < SPOT_CHECKS; ++i)
            check_one(&sampler, asset, scale, ref_below(random, width), ref_below(random, height),
                      tally);
    }
    check_outside(&sampler, asset, scale);
}

/* Stored alpha lookups match the planes inside the image and are zero elsewhere. */
static void check_alpha_at(const JelliAsset *asset, RefRandom *random)
{
    current_scale = 0u;
    unsigned density = ref_density(asset);
    unsigned width = asset->width * (density ? density : 1u);
    unsigned height = asset->height * (density ? density : 1u);
    for (unsigned i = 0u; i < 64u; ++i) {
        unsigned x = ref_below(random, width + 3u), y = ref_below(random, height + 3u);
        bool inside = ref_readable(asset) && x < width && y < height;
        CHECK_CASE(jelli_asset_alpha_at(asset, x, y) == (inside ? ref_texel(asset, x, y) : 0u));
    }
    CHECK_CASE(jelli_asset_alpha_at(asset, UINT_MAX, 0u) == 0u);
    CHECK_CASE(jelli_asset_density(asset) == ref_density(asset));
}

static void random_sprites_match_reference(void)
{
    Tally tally = {0};
    for (unsigned i = 0u; i < SPRITES; ++i) {
        current_seed = 0x5a3c0000u + i * 0x9e3779b9u;
        RefRandom random = {current_seed | 1u};
        RefSprite sprite;
        ref_sprite_random(&sprite, &random, 60000u + i, 1u + ref_below(&random, MAX_LOGICAL), true);
        check_alpha_at(&sprite.asset, &random);
        for (unsigned scale = 0u; scale <= 17u; ++scale)
            check_scale(&sprite.asset, scale, &random, &tally);
        ref_sprite_free(&sprite);
    }
    current_seed = 0u;
    CHECK_CASE(!jelli_asset_scale_supported(NULL, 1u) && jelli_asset_density(NULL) == 0u);
    CHECK_CASE(jelli_asset_alpha_at(NULL, 0u, 0u) == 0u && !jelli_asset_hit(NULL, 0u, 0u, 1u));
    JelliAssetSampler sampler;
    CHECK_CASE(!jelli_asset_sampler(&sampler, NULL, 1u));
    CHECK_CASE(tally.boxed > 1000u && tally.upscaled > 1000u && tally.soft > 100000u);
    CHECK_CASE(tally.refused > 5000u && tally.drawable > 5000u);
    printf("sampler: %u sprites x 18 scales: %u drawable, %u refused; %u samples (%u covered, "
           "%u soft), %u box, %u nearest\n",
           (unsigned)SPRITES, tally.drawable, tally.refused, tally.samples, tally.covered,
           tally.soft, tally.boxed, tally.upscaled);
}

/* Fills `hires` (density D) as the nearest upscale of `base` (density 1, alpha plane). */
static void upscale(RefSprite *hires, const RefSprite *base, unsigned density)
{
    unsigned width = base->asset.width, height = base->asset.height, pitch = width * density;
    ref_sprite_alpha(hires, base->asset.id + 1u, width, height, density);
    for (unsigned y = 0u; y < height * density; ++y)
        for (unsigned x = 0u; x < pitch; ++x) {
            size_t from = (size_t)(y / density) * width + x / density;
            hires->pixels[(size_t)y * pitch + x] = base->pixels[from];
            hires->alpha[(size_t)y * pitch + x] = base->alpha[from];
        }
}

/* Same output at every pixel: alpha always, colour wherever it is covered. */
static void check_same_samples(const JelliAsset *a, const JelliAsset *b, unsigned scale)
{
    current_scale = scale;
    JelliAssetSampler sa, sb;
    CHECK_CASE(jelli_asset_sampler(&sa, a, scale) && jelli_asset_sampler(&sb, b, scale));
    CHECK_CASE(sa.width == sb.width && sa.height == sb.height);
    for (unsigned y = 0u; y < sa.height; ++y)
        for (unsigned x = 0u; x < sa.width; ++x) {
            uint16_t ca = 0u, cb = 0u;
            unsigned alpha = jelli_asset_sample(&sa, x, y, &ca);
            CHECK_CASE(alpha == jelli_asset_sample(&sb, x, y, &cb));
            CHECK_CASE(!alpha || ca == cb);
            CHECK_CASE(jelli_asset_hit(a, x, y, scale) == jelli_asset_hit(b, x, y, scale));
        }
}

/* Density invariance: box-averaging uniform D x D blocks and nearest-upscaling both
 * reproduce the logical art exactly, so density is invisible for upscaled art. */
static void density_is_invisible_for_upscaled_art(void)
{
    unsigned compared = 0u;
    for (unsigned i = 0u; i < 300u; ++i) {
        current_seed = 0x0de75100u + i;
        RefRandom random = {current_seed * 2654435761u | 1u};
        RefSprite base, hires;
        unsigned width = 1u + ref_below(&random, 12u), height = 1u + ref_below(&random, 12u);
        ref_sprite_alpha(&base, 61000u, width, height, 1u);
        for (unsigned p = 0u; p < width * height; ++p)
            base.pixels[p] = (uint16_t)ref_next(&random);
        ref_fill_alpha(base.alpha, (size_t)width * height, &random);
        upscale(&hires, &base, 2u + ref_below(&random, JELLI_ASSET_MAX_DENSITY - 1u));
        for (unsigned scale = 1u; scale <= 16u; ++scale)
            if (jelli_asset_scale_supported(&hires.asset, scale)) {
                check_same_samples(&base.asset, &hires.asset, scale);
                ++compared;
            }
        ref_sprite_free(&base);
        ref_sprite_free(&hires);
    }
    CHECK_CASE(compared > 1000u);
    printf("density invariance: %u sprite/scale pairs identical\n", compared);
}

/* A 1-bit mask and the 0/255 alpha plane it stands for sample identically. */
static void mask_equals_binary_alpha(void)
{
    unsigned compared = 0u;
    for (unsigned i = 0u; i < 1500u; ++i) {
        current_seed = 0x3a5c0000u + i;
        RefRandom random = {current_seed * 2246822519u | 1u};
        RefSprite masked;
        ref_sprite_random(&masked, &random, 62000u, 1u + ref_below(&random, 20u), false);
        unsigned density = ref_density(&masked.asset);
        if (!masked.mask || !density) {
            ref_sprite_free(&masked);
            continue;
        }
        RefSprite binary;
        ref_sprite_alpha(&binary, 62001u, masked.asset.width, masked.asset.height, density);
        binary.asset.density = masked.asset.density;
        unsigned pitch = masked.asset.width * density;
        for (unsigned y = 0u; y < masked.asset.height * density; ++y)
            for (unsigned x = 0u; x < pitch; ++x) {
                binary.alpha[(size_t)y * pitch + x] = ref_texel(&masked.asset, x, y);
                binary.pixels[(size_t)y * pitch + x] = masked.pixels[(size_t)y * pitch + x];
            }
        for (unsigned scale = 1u; scale <= 16u; ++scale)
            if (jelli_asset_scale_supported(&masked.asset, scale))
                check_same_samples(&masked.asset, &binary.asset, scale);
        ref_sprite_free(&masked);
        ref_sprite_free(&binary);
        ++compared;
    }
    CHECK_CASE(compared > 300u);
    printf("mask invariance: %u masked sprites identical to binary alpha\n", compared);
}

int main(void)
{
    random_sprites_match_reference();
    density_is_invisible_for_upscaled_art();
    mask_equals_binary_alpha();
    puts("PASS: sampler matches the reference model; density and mask metamorphic relations");
    return 0;
}

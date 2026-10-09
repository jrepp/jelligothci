#include "jelli/assets.h"
#include "../core/pet_canvas.h" /* Internal canvas under test. */
#include "../core/pet_draw.h"   /* Night tint shared with the renderer. */

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

enum { STRIDE = JELLI_WIDTH + 3, PIXELS = STRIDE * JELLI_HEIGHT, BACK = 0x39e7 };
enum { LOGICAL = 4, DENSITY = 6, STORED = LOGICAL * DENSITY, HIRES_ID = 50001, LEGACY_ID = 50002 };
static uint16_t pixels[PIXELS], expected[PIXELS];
static uint16_t hires_pixels[STORED * STORED], legacy_pixels[LOGICAL * LOGICAL];
static uint8_t hires_alpha[STORED * STORED];
static const uint8_t legacy_mask[LOGICAL] = {0x60u, 0xf0u, 0x90u, 0xf0u};
static JelliAsset items[2];
static const JelliAssetSet set = {items, 2u, NULL};

/* Non-palette colours and every alpha class: clear, faint, half, strong and opaque. */
static void make_assets(void)
{
    static const uint8_t levels[] = {0u, 17u, 128u, 200u, 255u, 255u, 255u};
    for (unsigned y = 0u; y < STORED; ++y)
        for (unsigned x = 0u; x < STORED; ++x) {
            hires_pixels[y * STORED + x] =
                (uint16_t)(((x * 3u) % 32u) << 11 | ((y * 5u + x) % 64u) << 5 | (31u - y % 32u));
            hires_alpha[y * STORED + x] = levels[(x * 7u + y * 3u) % 7u];
        }
    for (unsigned i = 0u; i < LOGICAL * LOGICAL; ++i)
        legacy_pixels[i] = (uint16_t)(0x1234u * (i + 1u));
    items[0] = (JelliAsset){.id = HIRES_ID,
                            .width = LOGICAL,
                            .height = LOGICAL,
                            .pixels = hires_pixels,
                            .density = DENSITY,
                            .alpha = hires_alpha,
                            .right = LOGICAL,
                            .bottom = LOGICAL};
    items[1] = (JelliAsset){.id = LEGACY_ID,
                            .width = LOGICAL,
                            .height = LOGICAL,
                            .pixels = legacy_pixels,
                            .mask = legacy_mask,
                            .mask_stride = 1u,
                            .right = LOGICAL,
                            .bottom = LOGICAL};
}

static Canvas canvas(uint16_t *target, int left, int top, int right, int bottom)
{
    static JelliSurface surfaces[2];
    JelliSurface *s = &surfaces[target == pixels ? 0 : 1];
    *s = (JelliSurface){target, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    return (Canvas){s, left, top, right, bottom, 0u, false, &set};
}

static void fill(uint16_t *target, uint16_t color)
{
    for (size_t i = 0u; i < PIXELS; ++i)
        target[i] = color;
}

/* Independent reference: per-channel rounding blend, written without the canvas. */
static uint16_t ref_blend(uint16_t back, uint16_t front, unsigned alpha)
{
    unsigned out = 0u;
    static const unsigned shifts[3] = {11u, 5u, 0u}, masks[3] = {31u, 63u, 31u};
    for (unsigned c = 0u; c < 3u; ++c) {
        unsigned b = (back >> shifts[c]) & masks[c], f = (front >> shifts[c]) & masks[c];
        out |= ((f * alpha + b * (255u - alpha) + 127u) / 255u) << shifts[c];
    }
    return (uint16_t)out;
}

/* Reference sample of the hi-res fixture at output pixel (x, y) for a logical scale. */
static unsigned ref_sample(unsigned x, unsigned y, unsigned scale, uint16_t *color)
{
    if (scale >= DENSITY) {
        unsigned sx = x * DENSITY / scale, sy = y * DENSITY / scale;
        *color = hires_pixels[sy * STORED + sx];
        return hires_alpha[sy * STORED + sx];
    }
    unsigned k = DENSITY / scale, sum[4] = {0u, 0u, 0u, 0u};
    for (unsigned dy = 0u; dy < k; ++dy)
        for (unsigned dx = 0u; dx < k; ++dx) {
            unsigned at = (y * k + dy) * STORED + x * k + dx, a = hires_alpha[at];
            uint16_t texel = hires_pixels[at];
            sum[0] += a;
            sum[1] += (texel >> 11) * a;
            sum[2] += ((texel >> 5) & 63u) * a;
            sum[3] += (texel & 31u) * a;
        }
    unsigned alpha = (sum[0] + k * k / 2u) / (k * k);
    if (alpha)
        *color = (uint16_t)((sum[1] + sum[0] / 2u) / sum[0] << 11 |
                            (sum[2] + sum[0] / 2u) / sum[0] << 5 | (sum[3] + sum[0] / 2u) / sum[0]);
    return alpha;
}

static void ref_draw(int x0, int y0, unsigned scale, bool dim, uint8_t night)
{
    for (unsigned y = 0u; y < LOGICAL * scale; ++y)
        for (unsigned x = 0u; x < LOGICAL * scale; ++x) {
            int px = x0 + (int)x, py = y0 + (int)y;
            uint16_t color = 0u;
            unsigned alpha = ref_sample(x, y, scale, &color);
            if (!alpha || px < 0 || py < 0 || px >= JELLI_WIDTH || py >= JELLI_HEIGHT ||
                !jelli_canvas_in_round(px, py))
                continue;
            color = jelli_pet_night_color(color, night);
            if (dim)
                color = (uint16_t)((color >> 1) & 0x7befu);
            uint16_t *d = &expected[(unsigned)py * STRIDE + (unsigned)px];
            *d = ref_blend(*d, color, alpha);
        }
}

static void check_same(void)
{
    for (size_t i = 0u; i < PIXELS; ++i)
        CHECK(pixels[i] == expected[i]);
}

static void scale_rules(void)
{
    static const struct {
        unsigned scale;
        uint8_t density;
        bool ok;
    } cases[] = {{3u, 0u, true},  {6u, 1u, true},  {6u, 6u, true},  {12u, 6u, true},
                 {1u, 6u, true},  {2u, 6u, true},  {3u, 6u, true},  {4u, 6u, false},
                 {2u, 4u, true},  {4u, 4u, true},  {3u, 4u, false}, {6u, 4u, false},
                 {1u, 3u, true},  {2u, 3u, false}, {3u, 2u, false}, {0u, 6u, false},
                 {9u, 9u, false}, {16u, 8u, true}, {17u, 1u, false}};
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        JelliAsset a = items[0];
        a.density = cases[i].density;
        CHECK(jelli_asset_scale_supported(&a, cases[i].scale) == cases[i].ok);
    }
    CHECK(!jelli_asset_scale_supported(NULL, 1u) && jelli_asset_density(NULL) == 0u);
    JelliAsset hollow = items[0];
    hollow.alpha = NULL; /* No mask either: never readable. */
    JelliAssetSampler sampler = {0};
    CHECK(!jelli_asset_sampler(&sampler, &hollow, 6u) && sampler.asset == NULL);
    hollow = items[1];
    hollow.mask_stride = 0u; /* A mask row cannot be shorter than the stored row. */
    CHECK(!jelli_asset_sampler(&sampler, &hollow, 2u));
}

/* Box filtering weighs colour by alpha, so clear texels never tint an edge. */
static void box_is_premultiplied(void)
{
    static const uint16_t colors[4] = {0xf800u, 0x07e0u, 0x07e0u, 0x07e0u};
    static const uint8_t alphas[4] = {255u, 0u, 0u, 0u}, mixed[4] = {255u, 255u, 255u, 255u};
    static const uint16_t stripes[4] = {0xf800u, 0x001fu, 0xf800u, 0x001fu};
    JelliAsset a = {
        .id = 1u, .width = 1u, .height = 1u, .pixels = colors, .alpha = alphas, .density = 2u};
    JelliAssetSampler sampler;
    uint16_t color = 0u;
    CHECK(jelli_asset_sampler(&sampler, &a, 1u) && sampler.down == 2u && sampler.up == 1u);
    CHECK(jelli_asset_sample(&sampler, 0u, 0u, &color) == 64u && color == 0xf800u);
    a.pixels = stripes;
    a.alpha = mixed;
    CHECK(jelli_asset_sampler(&sampler, &a, 1u));
    CHECK(jelli_asset_sample(&sampler, 0u, 0u, &color) == 255u && color == (16u << 11 | 16u));
    CHECK(jelli_asset_sample(&sampler, 1u, 0u, &color) == 0u); /* Outside the output. */
    CHECK(jelli_asset_sampler(&sampler, &a, 2u) && sampler.up == 1u && sampler.down == 1u);
    CHECK(jelli_asset_sample(&sampler, 1u, 1u, &color) == 255u && color == 0x001fu);
    CHECK(jelli_asset_sampler(&sampler, &a, 6u) && sampler.up == 3u && sampler.width == 6u);
    CHECK(jelli_asset_sample(&sampler, 5u, 2u, &color) == 255u && color == 0x001fu);
}

static void blend_endpoints(void)
{
    CHECK(jelli_canvas_blend(0x1234u, 0xffffu, 0u) == 0x1234u);
    CHECK(jelli_canvas_blend(0x1234u, 0xabcdu, 255u) == 0xabcdu);
    CHECK(jelli_canvas_blend(0x1234u, 0xabcdu, 300u) == 0xabcdu);
    CHECK(jelli_canvas_blend(0x0000u, 0xffffu, 128u) == (16u << 11 | 32u << 5 | 16u));
    for (unsigned a = 0u; a <= 255u; a += 5u)
        CHECK(jelli_canvas_blend(0x39e7u, 0xd34du, a) == ref_blend(0x39e7u, 0xd34du, a));
}

/* 1:1, nearest upscale and every box downscale match the reference, including the
 * dim and night transforms applied to the source before blending. */
static void draws_match_reference(void)
{
    static const unsigned scales[] = {1u, 2u, 3u, 6u, 12u};
    for (size_t i = 0u; i < sizeof(scales) / sizeof(scales[0]); ++i)
        for (unsigned variant = 0u; variant < 4u; ++variant) {
            bool dim = variant & 1u;
            uint8_t night = variant & 2u ? 255u : 0u;
            fill(pixels, BACK);
            fill(expected, BACK);
            Canvas c = canvas(pixels, 0, 0, JELLI_WIDTH, JELLI_HEIGHT);
            c.dim = dim;
            c.icon_night = night;
            jelli_canvas_sprite(&c, HIRES_ID, 200, 190, scales[i]);
            ref_draw(200, 190, scales[i], dim, night);
            check_same();
        }
    fill(pixels, BACK);
    fill(expected, BACK);
    Canvas c = canvas(pixels, 0, 0, JELLI_WIDTH, JELLI_HEIGHT);
    jelli_canvas_sprite(&c, HIRES_ID, 200, 190, 4u); /* 6 % 4 and 4 % 6: rejected. */
    jelli_canvas_sprite(&c, 49999u, 200, 190, 6u);   /* Unknown ID. */
    check_same();
}

/* Corners outside the round panel and pixels outside the clip stay untouched; tiled
 * partial redraws over a repainted background equal one full draw. */
static void round_mask_clip_and_tiles(void)
{
    fill(pixels, BACK);
    fill(expected, BACK);
    Canvas c = canvas(pixels, 0, 0, JELLI_WIDTH, JELLI_HEIGHT);
    jelli_canvas_sprite(&c, HIRES_ID, -6, -6, 12u);
    ref_draw(-6, -6, 12u, false, 0u);
    check_same();
    CHECK(pixels[0] == BACK && pixels[30u * STRIDE + 30u] == BACK);
    fill(expected, BACK);
    c = canvas(expected, 0, 0, JELLI_WIDTH, JELLI_HEIGHT);
    jelli_canvas_sprite(&c, HIRES_ID, 213, 211, 6u);
    jelli_canvas_sprite(&c, LEGACY_ID, 225, 215, 3u);
    fill(pixels, 0xffffu);
    for (int top = 200; top < 245; top += 7)
        for (int left = 205; left < 245; left += 11) {
            Canvas tile = canvas(pixels, left, top, left + 11, top + 7);
            jelli_canvas_rect(&tile, left, top, 11, 7, BACK); /* Region repaint first. */
            jelli_canvas_sprite(&tile, HIRES_ID, 213, 211, 6u);
            jelli_canvas_sprite(&tile, LEGACY_ID, 225, 215, 3u);
        }
    for (int y = 0; y < JELLI_HEIGHT; ++y)
        for (int x = 0; x < JELLI_WIDTH; ++x) {
            bool tiled = x >= 205 && x < 249 && y >= 200 && y < 249;
            uint16_t value = pixels[(unsigned)y * STRIDE + (unsigned)x];
            CHECK(tiled ? value == expected[(unsigned)y * STRIDE + (unsigned)x] : value == 0xffffu);
        }
}

/* The pre-density renderer, kept verbatim as the reference for legacy art. */
static void old_sprite(Canvas *c, const JelliAsset *a, int x, int y, unsigned scale)
{
    for (unsigned row = 0; row < a->height; ++row)
        for (unsigned column = 0; column < a->width; ++column)
            if (a->mask[row * a->mask_stride + column / 8u] & (uint8_t)(1u << (7u - column % 8u)))
                jelli_canvas_rect(
                    c, x + (int)(column * scale), y + (int)(row * scale), (int)scale, (int)scale,
                    jelli_pet_night_color(a->pixels[row * a->width + column], c->icon_night));
}

static void legacy_is_unchanged(void)
{
    for (unsigned scale = 1u; scale <= 6u; ++scale)
        for (unsigned variant = 0u; variant < 4u; ++variant) {
            fill(pixels, BACK);
            fill(expected, BACK);
            Canvas now = canvas(pixels, 3, 5, 460, 300), before = canvas(expected, 3, 5, 460, 300);
            now.dim = before.dim = variant & 1u;
            now.icon_night = before.icon_night = variant & 2u ? 200u : 0u;
            jelli_canvas_sprite(&now, LEGACY_ID, 220, 200, scale);
            old_sprite(&before, &items[1], 220, 200, scale);
            jelli_canvas_sprite(&now, LEGACY_ID, 1, 280, scale); /* Clipped and corner. */
            old_sprite(&before, &items[1], 1, 280, scale);
            check_same();
        }
}

static void hit_uses_stored_alpha(void)
{
    const JelliAsset *a = &items[0];
    for (unsigned y = 0u; y < STORED; ++y)
        for (unsigned x = 0u; x < STORED; ++x) {
            bool covered = hires_alpha[y * STORED + x] >= JELLI_ASSET_OPAQUE_ALPHA;
            CHECK(jelli_asset_hit(a, x, y, 6u) == covered);
            CHECK(jelli_asset_hit(a, x * 2u + 1u, y * 2u, 12u) == covered);
            CHECK(jelli_asset_alpha_at(a, x, y) == hires_alpha[y * STORED + x]);
        }
    CHECK(!jelli_asset_hit(a, STORED, 0u, 6u) && !jelli_asset_hit(a, 0u, 0u, 4u));
    CHECK(jelli_asset_alpha_at(a, STORED, 0u) == 0u && jelli_asset_alpha_at(a, 0u, STORED) == 0u);
    CHECK(jelli_asset_hit(&items[1], 1u, 0u, 1u) && !jelli_asset_hit(&items[1], 0u, 0u, 1u));
    CHECK(jelli_asset_hit(&items[1], 17u, 8u, 6u) && !jelli_asset_hit(&items[1], 6u, 12u, 6u));
}

int main(void)
{
    make_assets();
    scale_rules();
    box_is_premultiplied();
    blend_endpoints();
    draws_match_reference();
    round_mask_clip_and_tiles();
    legacy_is_unchanged();
    hit_uses_stored_alpha();
    puts("PASS: density sampling, alpha blending, dim/night, round mask, clipping, legacy art");
    return 0;
}

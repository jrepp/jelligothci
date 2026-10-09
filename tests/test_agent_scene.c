#include "jelli/agent_scene.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* A small surface with row padding, so stride handling is exercised too. */
enum { W = 40, H = 30, STRIDE = 47, SENTINEL = 0x1234 };
static uint16_t screen[STRIDE * H], expected[STRIDE * H];

/* A 4x3 logical sprite at density 2: 8x6 stored pixels with mixed alpha. */
enum { ART_W = 8, ART_H = 6 };
static uint16_t art_pixels[ART_W * ART_H];
static uint8_t art_alpha[ART_W * ART_H];
static JelliAsset art;
static uint32_t rng = 0x9e3779b9u;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void make_art(void)
{
    static const uint8_t alphas[] = {0u, 1u, 64u, 128u, 200u, 254u, 255u};
    for (unsigned i = 0u; i < ART_W * ART_H; ++i) {
        art_pixels[i] = (uint16_t)next();
        art_alpha[i] = alphas[next() % sizeof(alphas)];
    }
    art = (JelliAsset){.id = 1u,
                       .width = 4u,
                       .height = 3u,
                       .pixels = art_pixels,
                       .right = 4u,
                       .bottom = 3u,
                       .density = 2u,
                       .alpha = art_alpha};
}

static JelliSurface surface_of(uint16_t *pixels)
{
    return (JelliSurface){pixels, W, H, STRIDE, {0u, 0u, 0u, 0u}};
}

static void fill(uint16_t *pixels, uint16_t value)
{
    for (unsigned i = 0u; i < STRIDE * H; ++i)
        pixels[i] = value;
}

/* Independent reference: every output pixel sampled on its own, over black. */
static uint16_t over_black(uint16_t color, unsigned alpha)
{
    unsigned r = (((color >> 11) & 31u) * alpha + 127u) / 255u;
    unsigned g = (((color >> 5) & 63u) * alpha + 127u) / 255u;
    unsigned b = ((color & 31u) * alpha + 127u) / 255u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void reference(uint16_t *pixels, int x, int y, unsigned scale, JelliRect clip)
{
    JelliAssetSampler sampler;
    bool ok = jelli_asset_sampler(&sampler, &art, scale);
    for (unsigned row = 0u; row < H; ++row) {
        for (unsigned column = 0u; column < W; ++column) {
            if (column < clip.x || column >= clip.x + clip.width || row < clip.y ||
                row >= clip.y + clip.height)
                continue;
            uint16_t color = 0u;
            uint8_t alpha = 0u;
            /* Pixels left of or above the sprite's origin are outside it. */
            if (ok && (long)column >= (long)x && (long)row >= (long)y)
                alpha = jelli_asset_sample(&sampler, (unsigned)((long)column - x),
                                           (unsigned)((long)row - y), &color);
            pixels[row * STRIDE + column] = alpha ? over_black(color, alpha) : 0u;
        }
    }
}

static bool same(void) { return memcmp(screen, expected, sizeof(screen)) == 0; }

static const JelliRect everything = {0u, 0u, W, H};

/* Whole draws match the reference at every scale, including off-screen positions. */
static void draws_match_the_reference(void)
{
    static const int positions[][2] = {{0, 0},     {-5, -3}, {10, 5},  {W - 3, H - 2},
                                       {W + 5, 0}, {0, -40}, {-15, 12}};
    static const unsigned scales[] = {1u, 2u, 4u, 6u};
    for (size_t p = 0u; p < sizeof(positions) / sizeof(positions[0]); ++p) {
        for (size_t k = 0u; k < sizeof(scales) / sizeof(scales[0]); ++k) {
            JelliSurface surface = surface_of(screen);
            fill(screen, SENTINEL);
            fill(expected, SENTINEL);
            CHECK(jelli_agent_scene_draw(&surface, &art, positions[p][0], positions[p][1],
                                         scales[k], everything));
            reference(expected, positions[p][0], positions[p][1], scales[k], everything);
            CHECK(same()); /* Row padding past the width keeps the sentinel too. */
        }
    }
}

static JelliRect random_rect(void)
{
    unsigned x = next() % (W + 6u), y = next() % (H + 6u);
    return (JelliRect){x, y, next() % (W + 8u), next() % (H + 8u)};
}

/* Moving the sprite and redrawing only the union of its old and new rectangles
 * yields exactly a fresh full draw at the new position (the "bob" update). */
static void union_updates_equal_full_redraws(void)
{
    for (unsigned trial = 0u; trial < 2000u; ++trial) {
        make_art();
        JelliSurface surface = surface_of(screen);
        unsigned scale = (next() % 2u) ? 4u : 2u;
        int ax = (int)(next() % 50u) - 10, ay = (int)(next() % 40u) - 10;
        int bx = (int)(next() % 50u) - 10, by = (int)(next() % 40u) - 10;
        fill(screen, 0u);
        CHECK(jelli_agent_scene_draw(&surface, &art, ax, ay, scale, everything));
        JelliRect damage =
            jelli_agent_rect_union(jelli_agent_sprite_rect(&surface, &art, ax, ay, scale),
                                   jelli_agent_sprite_rect(&surface, &art, bx, by, scale));
        CHECK(jelli_agent_scene_draw(&surface, &art, bx, by, scale, damage));
        fill(expected, 0u);
        reference(expected, bx, by, scale, everything);
        CHECK(same());
        /* Redrawing any clip of an unchanged frame changes nothing. */
        CHECK(jelli_agent_scene_draw(&surface, &art, bx, by, scale, random_rect()));
        CHECK(same());
    }
}

/* Pixels outside the clip are never written, even when the clip runs off-surface. */
static void writes_stay_inside_the_clip(void)
{
    for (unsigned trial = 0u; trial < 2000u; ++trial) {
        JelliSurface surface = surface_of(screen);
        JelliRect clip = random_rect();
        int x = (int)(next() % 60u) - 20, y = (int)(next() % 50u) - 20;
        fill(screen, SENTINEL);
        fill(expected, SENTINEL);
        CHECK(jelli_agent_scene_draw(&surface, &art, x, y, 4u, clip));
        reference(expected, x, y, 4u, clip);
        CHECK(same());
    }
}

static void unsupported_scales_only_clear(void)
{
    JelliSurface surface = surface_of(screen);
    JelliRect clip = {3u, 4u, 10u, 5u};
    fill(screen, SENTINEL);
    CHECK(!jelli_agent_scene_draw(&surface, &art, 0, 0, 3u, clip)); /* 3 does not suit density 2 */
    fill(expected, SENTINEL);
    reference(expected, 0, 0, 3u, clip); /* The reference sampler refuses too: all black. */
    CHECK(same());
    JelliRect none = jelli_agent_sprite_rect(&surface, &art, 0, 0, 3u);
    CHECK(none.width == 0u && none.height == 0u);
}

static void rectangles(void)
{
    JelliSurface surface = surface_of(screen);
    JelliRect r = jelli_agent_sprite_rect(&surface, &art, -3, 2, 4u); /* 16 x 12 */
    CHECK(r.x == 0u && r.y == 2u && r.width == 13u && r.height == 12u);
    r = jelli_agent_sprite_rect(&surface, &art, W - 4, H - 4, 4u);
    CHECK(r.x == W - 4u && r.y == H - 4u && r.width == 4u && r.height == 4u);
    r = jelli_agent_sprite_rect(&surface, &art, W, 0, 4u);
    CHECK(r.width == 0u && r.height == 0u);
    JelliRect a = {2u, 3u, 4u, 5u}, b = {10u, 1u, 2u, 2u}, empty = {7u, 7u, 0u, 9u};
    JelliRect u = jelli_agent_rect_union(a, b);
    CHECK(u.x == 2u && u.y == 1u && u.width == 10u && u.height == 7u);
    u = jelli_agent_rect_union(a, empty);
    CHECK(u.x == a.x && u.y == a.y && u.width == a.width && u.height == a.height);
    u = jelli_agent_rect_union(empty, b);
    CHECK(u.x == b.x && u.y == b.y && u.width == b.width && u.height == b.height);
}

int main(void)
{
    make_art();
    draws_match_the_reference();
    union_updates_equal_full_redraws();
    writes_stay_inside_the_clip();
    unsupported_scales_only_clear();
    rectangles();
    puts("Agent scene checks passed");
    return 0;
}

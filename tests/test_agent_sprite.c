#include "jelli/agent_scene.h"
#include "jelli/agent_sprite.h"
#include <math.h>
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

/* 5x4 logical at density 2 (10x8 stored): an opaque 4x4 block at stored (3, 2), a
 * half-transparent texel, and clear texels whose colour is garbage. */
enum { ART_W = 10, ART_H = 8, RED = 0xf800u, GREEN = 0x07e0u, GARBAGE = 0x5555u };
static uint16_t art_pixels[ART_W * ART_H];
static uint8_t art_alpha[ART_W * ART_H];
static JelliAsset art;

static void make_art(void)
{
    for (unsigned i = 0u; i < ART_W * ART_H; ++i) {
        art_pixels[i] = GARBAGE;
        art_alpha[i] = 0u;
    }
    for (unsigned y = 2u; y < 6u; ++y)
        for (unsigned x = 3u; x < 7u; ++x) {
            art_pixels[y * ART_W + x] = RED;
            art_alpha[y * ART_W + x] = 255u;
        }
    art_pixels[2u * ART_W + 7u] = GREEN;
    art_alpha[2u * ART_W + 7u] = 128u;
    art = (JelliAsset){.id = 1u,
                       .width = 5u,
                       .height = 4u,
                       .pixels = art_pixels,
                       .right = 5u,
                       .bottom = 4u,
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

static void content_and_reach(void)
{
    JelliRect content = jelli_agent_asset_content(&art);
    CHECK(content.x == 3u && content.y == 2u && content.width == 5u && content.height == 4u);
    /* Opaque (>= 128) texels reach from the content centre (5.5, 4) to the far
     * corners of texel (7, 2): dx 2.5, dy 2. */
    float reach = jelli_agent_asset_reach(&art, content);
    CHECK(fabsf(reach - sqrtf(2.5f * 2.5f + 2.0f * 2.0f)) < 1e-4f);
    JelliAsset empty = art;
    empty.pixels = NULL;
    CHECK(jelli_agent_asset_content(&empty).width == 0u);
    CHECK(jelli_agent_asset_reach(&empty, content) == 0.0f);
}

/* At its own size the opaque block comes back exactly; clear texels stay clear. */
static void same_size_is_exact(void)
{
    uint16_t pixels[ART_W * ART_H];
    uint8_t alpha[ART_W * ART_H];
    JelliAgentSprite sprite = {pixels, alpha, ART_W, ART_H};
    CHECK(jelli_agent_sprite_resample(&sprite, &art, (JelliRect){0u, 0u, ART_W, ART_H}));
    for (unsigned i = 0u; i < ART_W * ART_H; ++i) {
        CHECK(alpha[i] == art_alpha[i]);
        if (art_alpha[i])
            CHECK(pixels[i] == art_pixels[i]);
    }
}

/* Stretched 3x, the block's inside stays pure red at full alpha, and no edge picks up
 * the garbage colour of its clear neighbours. */
static void stretching_keeps_colour_and_never_tints(void)
{
    enum { SW = 15, SH = 12 };
    uint16_t pixels[SW * SH];
    uint8_t alpha[SW * SH];
    JelliAgentSprite sprite = {pixels, alpha, SW, SH};
    CHECK(jelli_agent_sprite_resample(&sprite, &art, (JelliRect){3u, 2u, 5u, 4u}));
    CHECK(alpha[5 * SW + 4] == 255u && pixels[5 * SW + 4] == RED);
    unsigned edges = 0u;
    for (unsigned i = 0u; i < SW * SH; ++i) {
        if (!alpha[i])
            continue;
        unsigned g = (pixels[i] >> 5) & 63u, b = pixels[i] & 31u;
        CHECK(b == 0u); /* Garbage would bring blue in; red and green have none. */
        if (alpha[i] < 255u && g == 0u)
            ++edges;
    }
    CHECK(edges > 0u); /* Soft edges exist and are pure red. */
}

static void bad_requests_leave_the_planes_alone(void)
{
    uint16_t pixels[4] = {7u, 7u, 7u, 7u};
    uint8_t alpha[4] = {9u, 9u, 9u, 9u};
    JelliAgentSprite sprite = {pixels, alpha, 2u, 2u};
    CHECK(!jelli_agent_sprite_resample(&sprite, &art, (JelliRect){0u, 0u, 0u, 4u}));
    CHECK(!jelli_agent_sprite_resample(&sprite, &art, (JelliRect){6u, 0u, 5u, 4u}));
    CHECK(!jelli_agent_sprite_resample(&sprite, &art, (JelliRect){0u, 5u, 4u, 4u}));
    JelliAgentSprite none = {pixels, alpha, 0u, 2u};
    CHECK(!jelli_agent_sprite_resample(&none, &art, (JelliRect){0u, 0u, 4u, 4u}));
    CHECK(!jelli_agent_sprite_resample(&sprite, NULL, (JelliRect){0u, 0u, 4u, 4u}));
    for (unsigned i = 0u; i < 4u; ++i)
        CHECK(pixels[i] == 7u && alpha[i] == 9u);
}

/* A reference blend over black, for comparing whole frames. */
static uint16_t over_black(uint16_t front, unsigned alpha)
{
    unsigned r = (((front >> 11) & 31u) * alpha + 127u) / 255u;
    unsigned g = (((front >> 5) & 63u) * alpha + 127u) / 255u;
    unsigned b = ((front & 31u) * alpha + 127u) / 255u;
    return alpha >= 255u ? front : (uint16_t)((r << 11) | (g << 5) | b);
}

/* The reference frame: the sprite at (x, y) over black, flipped when mirror is set. */
static void reference_frame(const JelliAgentSprite *sprite, int x, int y, bool mirror)
{
    fill(expected, SENTINEL);
    for (unsigned row = 0u; row < H; ++row)
        for (unsigned column = 0u; column < W; ++column) {
            long sx = (long)column - x, sy = (long)row - y;
            /* Negative offsets wrap to huge unsigned values, so one test covers both sides. */
            bool inside = (unsigned long)sx < sprite->width && (unsigned long)sy < sprite->height;
            size_t at = inside ? (size_t)sy * sprite->width +
                                     (size_t)(mirror ? (long)sprite->width - 1 - sx : sx)
                               : 0u;
            expected[row * STRIDE + column] =
                inside ? over_black(sprite->pixels[at], sprite->alpha[at]) : 0u;
        }
}

static void draws_mirror_clip_and_split(void)
{
    uint16_t pixels[ART_W * ART_H];
    uint8_t alpha[ART_W * ART_H];
    JelliAgentSprite sprite = {pixels, alpha, ART_W, ART_H};
    CHECK(jelli_agent_sprite_resample(&sprite, &art, (JelliRect){0u, 0u, ART_W, ART_H}));
    JelliSurface s = surface_of(screen);
    JelliRect all = {0u, 0u, W, H};
    for (int mirror = 0; mirror < 2; ++mirror) {
        for (int x = -12; x < W + 2; x += 7) {
            int y = x / 2 - 3;
            reference_frame(&sprite, x, y, mirror);
            fill(screen, SENTINEL);
            jelli_agent_sprite_draw(&s, &sprite, x, y, mirror, all);
            CHECK(!memcmp(screen, expected, sizeof(screen)));
            /* The same frame drawn as four clips, and nothing written outside a clip. */
            fill(screen, SENTINEL);
            JelliRect parts[] = {{0u, 0u, 13u, 30u}, {13u, 0u, 27u, 11u}, {13u, 11u, 27u, 19u}};
            for (unsigned i = 0u; i < 3u; ++i)
                jelli_agent_sprite_draw(&s, &sprite, x, y, mirror, parts[i]);
            CHECK(!memcmp(screen, expected, sizeof(screen)));
        }
    }
    fill(screen, SENTINEL);
    jelli_agent_sprite_draw(&s, &sprite, 5, 5, false, (JelliRect){8u, 8u, 4u, 3u});
    for (unsigned row = 0u; row < H; ++row)
        for (unsigned column = 0u; column < STRIDE; ++column) {
            bool inside = column >= 8u && column < 12u && row >= 8u && row < 11u;
            CHECK(inside || screen[row * STRIDE + column] == SENTINEL);
        }
}

static void areas_and_intersections(void)
{
    uint16_t pixels[ART_W * ART_H];
    uint8_t alpha[ART_W * ART_H];
    JelliAgentSprite sprite = {pixels, alpha, ART_W, ART_H};
    JelliSurface s = surface_of(screen);
    JelliRect a = jelli_agent_sprite_area(&s, &sprite, -4, 25);
    CHECK(a.x == 0u && a.y == 25u && a.width == 6u && a.height == 5u);
    CHECK(jelli_agent_sprite_area(&s, &sprite, W, 0).width == 0u);
    CHECK(jelli_agent_sprite_area(&s, &sprite, -ART_W, 0).width == 0u);
    JelliRect i =
        jelli_agent_rect_intersect((JelliRect){2u, 3u, 10u, 10u}, (JelliRect){8u, 1u, 10u, 5u});
    CHECK(i.x == 8u && i.y == 3u && i.width == 4u && i.height == 3u);
    JelliRect none =
        jelli_agent_rect_intersect((JelliRect){0u, 0u, 2u, 2u}, (JelliRect){5u, 5u, 1u, 1u});
    CHECK(!none.x && !none.y && !none.width && !none.height);
}

int main(void)
{
    make_art();
    content_and_reach();
    same_size_is_exact();
    stretching_keeps_colour_and_never_tints();
    bad_requests_leave_the_planes_alone();
    draws_mirror_clip_and_split();
    areas_and_intersections();
    puts("Agent sprite checks passed");
    return 0;
}

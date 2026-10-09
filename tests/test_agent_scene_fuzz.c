/* Seeded property and metamorphic tests for the agent scene (core/agent/scene.c):
 * random sprites from the independent reference model (tests/asset_reference.h), every
 * scale 0..17, positions out to INT_MIN/INT_MAX, clips whose unsigned fields overflow a
 * 32-bit sum, and random strides. A failure prints the seed and scale so it replays. */
#include "asset_reference.h"
#include "jelli/agent_scene.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_CASE(expr, seed, scale)                                                              \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed %u, scale %u)\n", __FILE__, __LINE__, #expr,          \
                    (unsigned)(seed), (unsigned)(scale));                                          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* GUARD cells on each side of the surface's own memory must never change. */
enum { SEEDS = 6000u, MAX_W = 48u, MAX_H = 40u, GUARD = 32u, MAX_SCALE = 17u };

typedef struct {
    JelliSurface surface;
    uint16_t *block;  /* GUARD + stride * height + GUARD cells. */
    uint16_t *before; /* The block's random contents before drawing. */
    size_t cells;
    int x, y;
    JelliRect clip;
} Case;

/* Mostly small values near the surface, plus 0, 2^31 and values near UINT_MAX. */
static unsigned random_field(RefRandom *random, unsigned extent)
{
    switch (ref_below(random, 10u)) {
    case 0u:
        return UINT_MAX - ref_below(random, 64u);
    case 1u:
        return 0x80000000u + ref_below(random, 64u) - 32u;
    case 2u:
        return 0u;
    default:
        return ref_below(random, extent + 12u);
    }
}

/* A whole-surface clip a quarter of the time; otherwise four random fields. */
static JelliRect random_clip(RefRandom *random, unsigned width, unsigned height)
{
    if (ref_below(random, 4u) == 0u)
        return (JelliRect){0u, 0u, width, height};
    return (JelliRect){random_field(random, width), random_field(random, height),
                       random_field(random, width), random_field(random, height)};
}

/* Near INT_MIN or INT_MAX, far off either edge, or overlapping the surface. */
static int random_position(RefRandom *random, unsigned extent)
{
    switch (ref_below(random, 10u)) {
    case 0u:
        return INT_MIN + (int)ref_below(random, 300u);
    case 1u:
        return INT_MAX - (int)ref_below(random, 300u);
    case 2u:
        return (int)ref_below(random, 400u) - 600;
    default:
        return (int)ref_below(random, extent + 40u) - 36;
    }
}

static void make_case(Case *c, RefRandom *random)
{
    unsigned width = 1u + ref_below(random, MAX_W), height = 1u + ref_below(random, MAX_H);
    unsigned stride = width + ref_below(random, 9u);
    c->cells = GUARD + (size_t)stride * height + GUARD;
    c->block = malloc(c->cells * sizeof(uint16_t));
    c->before = malloc(c->cells * sizeof(uint16_t));
    if (!c->block || !c->before) {
        fputs("test_agent_scene_fuzz: out of memory\n", stderr);
        exit(1);
    }
    for (size_t i = 0u; i < c->cells; ++i)
        c->before[i] = (uint16_t)ref_next(random);
    c->surface = (JelliSurface){c->block + GUARD, width, height, stride, {0u, 0u, 0u, 0u}};
    c->x = random_position(random, width);
    c->y = random_position(random, height);
    c->clip = random_clip(random, width, height);
}

static bool in_span(unsigned start, unsigned length, unsigned value)
{
    return value >= start && (uint64_t)value < (uint64_t)start + length;
}

/* One surface pixel after the draw: untouched outside the clip, black inside it, and
 * the reference sample over black where the sprite covers it. */
static uint16_t expected_pixel(const Case *c, const JelliAsset *asset, unsigned scale,
                               unsigned column, unsigned row, uint16_t before)
{
    if (!in_span(c->clip.x, c->clip.width, column) || !in_span(c->clip.y, c->clip.height, row))
        return before;
    if (!ref_supported(asset, scale) || !ref_readable(asset))
        return 0u;
    int64_t dx = (int64_t)column - c->x, dy = (int64_t)row - c->y;
    if (dx < 0 || dy < 0 || dx >= (int64_t)asset->width * scale ||
        dy >= (int64_t)asset->height * scale)
        return 0u;
    uint16_t color = 0u;
    unsigned alpha = ref_sample(asset, scale, (unsigned)dx, (unsigned)dy, &color);
    return alpha ? ref_blend(0u, color, alpha) : 0u;
}

/* Compares the whole block: guards and row padding unchanged, every pixel as modelled. */
static bool matches(const Case *c, const JelliAsset *asset, unsigned scale)
{
    const JelliSurface *s = &c->surface;
    for (size_t i = 0u; i < c->cells; ++i) {
        size_t at = i - GUARD, row = at / s->stride, column = at % s->stride;
        bool pixel = i >= GUARD && i < c->cells - GUARD && column < s->width;
        uint16_t want =
            pixel ? expected_pixel(c, asset, scale, (unsigned)column, (unsigned)row, c->before[i])
                  : c->before[i];
        if (c->block[i] != want)
            return false;
    }
    return true;
}

/* The sprite rectangle, computed independently in 64 bits. */
static bool rect_matches(const Case *c, const JelliAsset *asset, unsigned scale)
{
    JelliRect got = jelli_agent_sprite_rect(&c->surface, asset, c->x, c->y, scale);
    int64_t x0 = c->x, y0 = c->y;
    int64_t x1 = x0 + (int64_t)asset->width * scale, y1 = y0 + (int64_t)asset->height * scale;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > (int64_t)c->surface.width ? (int64_t)c->surface.width : x1;
    y1 = y1 > (int64_t)c->surface.height ? (int64_t)c->surface.height : y1;
    if (!ref_supported(asset, scale) || !ref_readable(asset) || x1 <= x0 || y1 <= y0)
        return got.x == 0u && got.y == 0u && got.width == 0u && got.height == 0u;
    return got.x == (unsigned)x0 && got.y == (unsigned)y0 && got.width == (unsigned)(x1 - x0) &&
           got.height == (unsigned)(y1 - y0);
}

/* Metamorphic: the clip split in two at a random column, drawn piece by piece, leaves
 * the same pixels as the whole draw (only when the clip's x end fits 32 bits, so the
 * second piece's start is representable). */
static void split_matches(const Case *c, const JelliAsset *asset, unsigned scale, uint32_t seed,
                          RefRandom *random)
{
    if ((uint64_t)c->clip.x + c->clip.width > UINT_MAX)
        return;
    unsigned cut =
        c->clip.width ? (unsigned)(ref_next(random) % ((uint64_t)c->clip.width + 1u)) : 0u;
    JelliRect left = {c->clip.x, c->clip.y, cut, c->clip.height};
    JelliRect right = {c->clip.x + cut, c->clip.y, c->clip.width - cut, c->clip.height};
    uint16_t *whole = malloc(c->cells * sizeof(uint16_t));
    CHECK_CASE(whole != NULL, seed, scale);
    memcpy(whole, c->block, c->cells * sizeof(uint16_t));
    memcpy(c->block, c->before, c->cells * sizeof(uint16_t));
    bool a = jelli_agent_scene_draw(&c->surface, asset, c->x, c->y, scale, right);
    bool b = jelli_agent_scene_draw(&c->surface, asset, c->x, c->y, scale, left);
    CHECK_CASE(a == b, seed, scale);
    CHECK_CASE(memcmp(whole, c->block, c->cells * sizeof(uint16_t)) == 0, seed, scale);
    free(whole);
}

static void run_seed(uint32_t seed)
{
    RefRandom random = {seed};
    RefSprite sprite;
    Case c;
    ref_sprite_random(&sprite, &random, 1u, 12u, true);
    make_case(&c, &random);
    for (unsigned scale = 0u; scale <= MAX_SCALE; ++scale) {
        bool drawable = ref_supported(&sprite.asset, scale) && ref_readable(&sprite.asset);
        memcpy(c.block, c.before, c.cells * sizeof(uint16_t));
        bool drawn = jelli_agent_scene_draw(&c.surface, &sprite.asset, c.x, c.y, scale, c.clip);
        CHECK_CASE(drawn == drawable, seed, scale);
        CHECK_CASE(matches(&c, &sprite.asset, scale), seed, scale);
        CHECK_CASE(rect_matches(&c, &sprite.asset, scale), seed, scale);
        if (scale % 5u == seed % 5u) /* A rotating subset keeps the run short. */
            split_matches(&c, &sprite.asset, scale, seed, &random);
    }
    free(c.block);
    free(c.before);
    ref_sprite_free(&sprite);
}

static bool same_rect(JelliRect a, JelliRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

/* Union of two in-surface rectangles: symmetric, an empty one adds nothing, and
 * otherwise each edge is the outer edge of the pair. */
static void check_union(JelliRect a, JelliRect b, unsigned trial)
{
    JelliRect u = jelli_agent_rect_union(a, b);
    bool a_empty = !a.width || !a.height, b_empty = !b.width || !b.height;
    if (a_empty && b_empty) { /* Either may come back, but it is empty. */
        CHECK_CASE(!u.width || !u.height, trial, 0u);
        return;
    }
    CHECK_CASE(same_rect(u, jelli_agent_rect_union(b, a)), trial, 0u);
    if (a_empty || b_empty) {
        CHECK_CASE(same_rect(u, a_empty ? b : a), trial, 0u);
        return;
    }
    unsigned right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
    unsigned bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
    CHECK_CASE(u.x == (a.x < b.x ? a.x : b.x) && u.y == (a.y < b.y ? a.y : b.y), trial, 0u);
    CHECK_CASE(u.x + u.width == right && u.y + u.height == bottom, trial, 0u);
}

static JelliRect small_rect(RefRandom *random)
{
    return (JelliRect){ref_below(random, 500u), ref_below(random, 500u), ref_below(random, 500u),
                       ref_below(random, 500u)};
}

static void union_properties(void)
{
    RefRandom random = {0x51ce5eedu};
    for (unsigned trial = 0u; trial < 20000u; ++trial) {
        JelliRect a = small_rect(&random);
        check_union(a, small_rect(&random), trial);
    }
}

int main(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed)
        run_seed(seed * 2654435761u | 1u);
    union_properties();
    puts("Agent scene fuzz checks passed");
    return 0;
}

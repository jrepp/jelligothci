/* Property and metamorphic tests for the agent sprite's draw, area, content and reach,
 * and for the resampler's refusals (core/agent/sprite.c): random surfaces with row
 * padding, positions far off the surface, mirrored draws, and random clip splits. Each
 * case prints its seed. */
#include "agent_sprite_art.h"
#include "jelli/agent_scene.h"
#include <limits.h>
#include <math.h>
#include <string.h>

enum { SEEDS = 600u, SENTINEL = 0xbeefu };

typedef struct {
    uint16_t *pixels;
    JelliSurface surface;
    size_t count;
} Screen;

static Screen screen_make(uint32_t *state)
{
    unsigned w = 1u + art_below(state, 64u), h = 1u + art_below(state, 48u);
    unsigned stride = w + art_below(state, 8u);
    Screen s = {art_calloc((size_t)stride * h, sizeof(uint16_t)), {0}, (size_t)stride * h};
    s.surface = (JelliSurface){s.pixels, w, h, stride, {0u, 0u, 0u, 0u}};
    return s;
}

static void screen_fill(Screen *s, uint16_t value)
{
    for (size_t i = 0u; i < s->count; ++i)
        s->pixels[i] = value;
}

/* Random planes: a mix of clear (with garbage colour), opaque and partial pixels. */
static Planes random_planes(uint32_t *state)
{
    Planes p = planes_make(1u + art_below(state, 40u), 1u + art_below(state, 40u));
    for (size_t i = 0u; i < planes_count(&p); ++i) {
        unsigned pick = art_below(state, 4u);
        p.alpha[i] = pick == 0u ? 0u : pick == 1u ? 255u : (uint8_t)art_below(state, 256u);
        p.pixels[i] = (uint16_t)art_next(state);
    }
    return p;
}

/* Near the surface usually; sometimes at the far ends of int. */
static int random_position(uint32_t *state, unsigned extent, unsigned size)
{
    static const int far[] = {INT_MIN, INT_MIN + 1, -1000000000, 1000000000, INT_MAX - 1, INT_MAX};
    if (art_below(state, 8u) == 0u)
        return far[art_below(state, 6u)];
    return (int)art_below(state, extent + 2u * size + 6u) - (int)size - 3;
}

static JelliRect random_clip(uint32_t *state, const JelliSurface *s)
{
    switch (art_below(state, 6u)) {
    case 0:
        return (JelliRect){0u, 0u, s->width, s->height};
    case 1:
        return (JelliRect){0u, 0u, UINT_MAX, UINT_MAX};
    case 2:
        return (JelliRect){UINT_MAX - art_below(state, 4u), art_below(state, 4u), 10u, UINT_MAX};
    default: {
        unsigned x = art_below(state, s->width + 4u), y = art_below(state, s->height + 4u);
        return (JelliRect){x, y, art_below(state, s->width + 4u), art_below(state, s->height + 4u)};
    }
    }
}

static uint16_t over_black(uint16_t front, unsigned alpha)
{
    if (alpha >= 255u)
        return front;
    unsigned r = (((front >> 11) & 31u) * alpha + 127u) / 255u;
    unsigned g = (((front >> 5) & 63u) * alpha + 127u) / 255u;
    unsigned b = ((front & 31u) * alpha + 127u) / 255u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static bool within(int64_t value, int64_t start, uint64_t length)
{
    return value >= start && (uint64_t)(value - start) < length;
}

/* What draw must leave: the clip (inside the surface, never the padding) black with the
 * sprite blended over it; every other element of the buffer as it was. */
static void reference_draw(Screen *expected, const Planes *p, int x, int y, bool mirror,
                           JelliRect clip)
{
    const JelliSurface *s = &expected->surface;
    for (unsigned row = 0u; row < s->height; ++row)
        for (unsigned col = 0u; col < s->width; ++col) {
            if (!within(col, clip.x, clip.width) || !within(row, clip.y, clip.height))
                continue;
            uint16_t value = 0u;
            if (within(col, x, p->sprite.width) && within(row, y, p->sprite.height)) {
                uint64_t sx = (uint64_t)((int64_t)col - x), sy = (uint64_t)((int64_t)row - y);
                size_t at = sy * p->sprite.width + (mirror ? p->sprite.width - 1u - sx : sx);
                value = p->alpha[at] ? over_black(p->pixels[at], p->alpha[at]) : 0u;
            }
            expected->pixels[(size_t)row * s->stride + col] = value;
        }
}

static void draw_matches_the_reference(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 2246822519u;
        Screen got = screen_make(&state), expected = got;
        expected.pixels = art_calloc(got.count, sizeof(uint16_t));
        expected.surface.pixels = expected.pixels;
        Planes p = random_planes(&state);
        int x = random_position(&state, got.surface.width, p.sprite.width);
        int y = random_position(&state, got.surface.height, p.sprite.height);
        bool mirror = art_below(&state, 2u);
        JelliRect clip = random_clip(&state, &got.surface);
        screen_fill(&got, SENTINEL);
        screen_fill(&expected, SENTINEL);
        jelli_agent_sprite_draw(&got.surface, &p.sprite, x, y, mirror, clip);
        reference_draw(&expected, &p, x, y, mirror, clip);
        CHECK_CASE(!memcmp(got.pixels, expected.pixels, got.count * sizeof(uint16_t)), seed);
        free(expected.pixels);
        free(got.pixels);
        planes_free(&p);
    }
}

/* Splits r into parts (guillotine cuts) that tile it exactly. */
static unsigned split(uint32_t *state, JelliRect r, JelliRect *parts, unsigned room)
{
    if (room < 2u || r.width * r.height < 2u || art_below(state, 4u) == 0u) {
        parts[0] = r;
        return 1u;
    }
    bool across = r.width < 2u ? false : r.height < 2u ? true : art_below(state, 2u);
    JelliRect a = r, b = r;
    unsigned cut = 1u + art_below(state, (across ? r.width : r.height) - 1u);
    if (across) {
        a.width = cut;
        b.x += cut;
        b.width -= cut;
    } else {
        a.height = cut;
        b.y += cut;
        b.height -= cut;
    }
    unsigned n = split(state, a, parts, room / 2u);
    return n + split(state, b, parts + n, room - n);
}

/* Metamorphic: a frame drawn as random tiles, then as random overlapping clips over
 * those tiles, equals the frame drawn whole; mirroring equals a pre-flipped sprite. */
static void splits_and_mirrors_agree(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 3266489917u;
        Screen whole = screen_make(&state), tiled = whole;
        tiled.pixels = art_calloc(whole.count, sizeof(uint16_t));
        tiled.surface.pixels = tiled.pixels;
        Planes p = random_planes(&state), flipped = planes_make(p.sprite.width, p.sprite.height);
        for (unsigned row = 0u; row < p.sprite.height; ++row)
            for (unsigned col = 0u; col < p.sprite.width; ++col) {
                size_t from = (size_t)row * p.sprite.width + col;
                size_t to = (size_t)row * p.sprite.width + p.sprite.width - 1u - col;
                flipped.pixels[to] = p.pixels[from];
                flipped.alpha[to] = p.alpha[from];
            }
        int x = random_position(&state, whole.surface.width, p.sprite.width);
        int y = random_position(&state, whole.surface.height, p.sprite.height);
        bool mirror = art_below(&state, 2u);
        JelliRect all = {0u, 0u, whole.surface.width, whole.surface.height}, parts[16];
        screen_fill(&whole, SENTINEL);
        screen_fill(&tiled, SENTINEL);
        jelli_agent_sprite_draw(&whole.surface, &p.sprite, x, y, mirror, all);
        unsigned count = split(&state, all, parts, 16u);
        for (unsigned i = 0u; i < count; ++i)
            jelli_agent_sprite_draw(&tiled.surface, &p.sprite, x, y, mirror, parts[i]);
        for (unsigned i = art_below(&state, 4u); i > 0u; --i)
            jelli_agent_sprite_draw(&tiled.surface, &p.sprite, x, y, mirror,
                                    random_clip(&state, &tiled.surface));
        CHECK_CASE(!memcmp(whole.pixels, tiled.pixels, whole.count * sizeof(uint16_t)), seed);
        screen_fill(&tiled, SENTINEL);
        jelli_agent_sprite_draw(&tiled.surface, &flipped.sprite, x, y, !mirror, all);
        CHECK_CASE(!memcmp(whole.pixels, tiled.pixels, whole.count * sizeof(uint16_t)), seed);
        planes_free(&flipped);
        planes_free(&p);
        free(tiled.pixels);
        free(whole.pixels);
    }
}

/* The area is the sprite's rectangle clipped to the surface, from 64-bit arithmetic. */
static void area_is_the_clipped_rectangle(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 668265263u;
        Screen s = screen_make(&state);
        Planes p = random_planes(&state);
        int x = random_position(&state, s.surface.width, p.sprite.width);
        int y = random_position(&state, s.surface.height, p.sprite.height);
        JelliRect a = jelli_agent_sprite_area(&s.surface, &p.sprite, x, y);
        int64_t x0 = x > 0 ? x : 0, y0 = y > 0 ? y : 0;
        int64_t x1 = (int64_t)x + p.sprite.width, y1 = (int64_t)y + p.sprite.height;
        x1 = x1 < s.surface.width ? x1 : s.surface.width;
        y1 = y1 < s.surface.height ? y1 : s.surface.height;
        bool empty = x1 <= x0 || y1 <= y0;
        CHECK_CASE(empty ? !a.x && !a.y && !a.width && !a.height
                         : a.x == x0 && a.y == y0 && a.width == x1 - x0 && a.height == y1 - y0,
                   seed);
        CHECK_CASE((uint64_t)a.x + a.width <= s.surface.width, seed);
        CHECK_CASE((uint64_t)a.y + a.height <= s.surface.height, seed);
        planes_free(&p);
        free(s.pixels);
    }
}

/* Content and reach against brute force over random assets, including empty, full,
 * mask, faint (alpha below the opaque threshold) and unreadable ones. */
static void content_and_reach_match_brute_force(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 374761393u;
        Art art = art_random(&state);
        JelliRect c = jelli_agent_asset_content(&art.asset);
        unsigned left = UINT_MAX, top = UINT_MAX, right = 0u, bottom = 0u;
        double far = 0.0;
        JelliRect area = art_source(&state, &art);
        double cx = area.x + area.width / 2.0, cy = area.y + area.height / 2.0;
        for (unsigned y = 0u; y < art.height; ++y)
            for (unsigned x = 0u; x < art.width; ++x) {
                uint8_t a = art_alpha(&art, (long)x, (long)y);
                if (a) {
                    left = x < left ? x : left;
                    top = y < top ? y : top;
                    right = x + 1u > right ? x + 1u : right;
                    bottom = y + 1u > bottom ? y + 1u : bottom;
                }
                /* Farthest corner of every opaque texel from the area's centre. */
                for (unsigned k = 0u; a >= 128u && k < 4u; ++k) {
                    double dx = x + (k & 1u) - cx, dy = y + (k >> 1u) - cy;
                    far = dx * dx + dy * dy > far ? dx * dx + dy * dy : far;
                }
            }
        if (right)
            CHECK_CASE(c.x == left && c.y == top && c.width == right - left &&
                           c.height == bottom - top,
                       seed);
        else
            CHECK_CASE(!c.x && !c.y && !c.width && !c.height, seed);
        double reach = (double)jelli_agent_asset_reach(&art.asset, area);
        CHECK_CASE(fabs(reach - sqrt(far)) <= 1e-4 * (1.0 + sqrt(far)), seed);
        art_free(&art);
    }
}

static void content_and_reach_at_the_extremes(void)
{
    uint32_t state = 1u;
    Art full = art_make(&state, 7u, 5u, 2u, false, ART_OPAQUE);
    JelliRect c = jelli_agent_asset_content(&full.asset);
    CHECK_CASE(c.x == 0u && c.y == 0u && c.width == 14u && c.height == 10u, 0u);
    CHECK_CASE(fabsf(jelli_agent_asset_reach(&full.asset, c) - sqrtf(7.0f * 7.0f + 5.0f * 5.0f)) <
                   1e-4f,
               0u);
    memset(full.alpha, 127, (size_t)full.width * full.height); /* covered, never opaque */
    CHECK_CASE(jelli_agent_asset_content(&full.asset).width == 14u, 0u);
    CHECK_CASE(jelli_agent_asset_reach(&full.asset, c) == 0.0f, 0u);
    full.alpha[0] = 128u; /* exactly the threshold counts: its far corner is the image corner */
    CHECK_CASE(fabsf(jelli_agent_asset_reach(&full.asset, c) - sqrtf(7.0f * 7.0f + 5.0f * 5.0f)) <
                   1e-4f,
               0u);
    memset(full.alpha, 0, (size_t)full.width * full.height);
    CHECK_CASE(jelli_agent_asset_content(&full.asset).width == 0u, 0u);
    CHECK_CASE(jelli_agent_asset_reach(&full.asset, c) == 0.0f, 0u);
    JelliAsset broken[4] = {full.asset, full.asset, full.asset, full.asset};
    broken[0].density = 9u; /* beyond the largest density */
    broken[1].alpha = NULL; /* no coverage plane at all */
    broken[2].width = 0u;
    broken[3].pixels = NULL;
    for (unsigned i = 0u; i < 4u; ++i) {
        CHECK_CASE(jelli_agent_asset_content(&broken[i]).width == 0u, i);
        CHECK_CASE(jelli_agent_asset_reach(&broken[i], c) == 0.0f, i);
    }
    CHECK_CASE(jelli_agent_asset_content(NULL).width == 0u, 0u);
    art_free(&full);
    /* A mask whose rows are too short to hold the image covers nothing. */
    Art masked = art_make(&state, 12u, 3u, 1u, true, ART_OPAQUE);
    masked.asset.mask_stride = 1u;
    CHECK_CASE(jelli_agent_asset_content(&masked.asset).width == 0u, 0u);
    CHECK_CASE(jelli_agent_asset_reach(&masked.asset, c) == 0.0f, 0u);
    art_free(&masked);
}

/* Every refused request leaves both planes untouched; the exact edges are accepted. */
static void refusals_leave_the_planes_alone(void)
{
    uint32_t state = 5u;
    Art art = art_make(&state, 6u, 4u, 2u, false, ART_NOISE); /* 12 x 8 stored */
    Planes p = planes_make(3u, 2u);
    const JelliRect bad[] = {{0u, 0u, 0u, 8u},       {0u, 0u, 12u, 0u},
                             {1u, 0u, 12u, 8u},      {0u, 1u, 12u, 8u},
                             {12u, 0u, 1u, 1u},      {0u, 8u, 1u, 1u},
                             {UINT_MAX, 0u, 2u, 1u}, {0u, UINT_MAX, 1u, 2u},
                             {2u, 0u, UINT_MAX, 1u}, {0u, 0u, UINT_MAX, UINT_MAX}};
    JelliAgentSprite sprites[] = {p.sprite, p.sprite, p.sprite, p.sprite};
    sprites[1].pixels = NULL;
    sprites[2].alpha = NULL;
    sprites[3].height = 0u;
    for (unsigned i = 0u; i < sizeof bad / sizeof bad[0] + 4u; ++i) {
        memset(p.pixels, 0x5a, planes_count(&p) * sizeof(uint16_t));
        memset(p.alpha, 0x5a, planes_count(&p));
        bool ok = i < 10u ? jelli_agent_sprite_resample(&p.sprite, &art.asset, bad[i])
                  : i < 13u
                      ? jelli_agent_sprite_resample(&sprites[i - 9u], &art.asset,
                                                    (JelliRect){0u, 0u, 2u, 2u})
                      : jelli_agent_sprite_resample(NULL, &art.asset, (JelliRect){0u, 0u, 2u, 2u});
        CHECK_CASE(!ok, i);
        for (size_t k = 0u; k < planes_count(&p); ++k)
            CHECK_CASE(p.pixels[k] == 0x5a5au && p.alpha[k] == 0x5au, i);
    }
    CHECK_CASE(jelli_agent_sprite_resample(&p.sprite, &art.asset, (JelliRect){0u, 0u, 12u, 8u}),
               0u);
    CHECK_CASE(jelli_agent_sprite_resample(&p.sprite, &art.asset, (JelliRect){11u, 7u, 1u, 1u}),
               0u);
    planes_free(&p);
    art_free(&art);
}

int main(void)
{
    draw_matches_the_reference();
    splits_and_mirrors_agree();
    area_is_the_clipped_rectangle();
    content_and_reach_match_brute_force();
    content_and_reach_at_the_extremes();
    refusals_leave_the_planes_alone();
    printf("Agent sprite draw and measure properties passed (%u seeds each)\n", (unsigned)SEEDS);
    return 0;
}

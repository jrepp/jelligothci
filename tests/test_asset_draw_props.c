/* Seeded property tests for the sprite compositor (core/pet_canvas.c) and the magic
 * particle pass (core/pet_particles.c) over random synthetic sprites at every scale.
 * Each draw lands on a surface whose stride padding holds a sentinel and is compared,
 * pixel for pixel, with a reference compositor built on asset_reference.c: only the
 * clip rectangle and round panel change, the night tint and dimming apply to the
 * source colour before the straight-alpha blend, and unsupported scale/density pairs
 * and incomplete descriptors draw nothing. Metamorphic: drawing the clip as four tiles
 * equals one draw (the partial-redraw contract). Magic particles fade sampled alpha
 * by life and never paint outside jelli_particles_bounds, the damage they report.
 * Surfaces and planes are exact-size heap blocks for AddressSanitizer. A failure
 * prints the seed. */
#include "asset_reference.h"
#include "../core/pet_canvas.h" /* Internal compositor under test. */
#include "../core/pet_draw.h"   /* Night tint and the particle pass. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_CASE(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x)\n", __FILE__, __LINE__, #expression,          \
                    (unsigned)current_seed);                                                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { DRAWS = 700, SENTINEL = 0xa55a, MAX_PAD = 5 };
static uint32_t current_seed;

typedef struct {
    uint16_t *got, *want;
    unsigned stride;
    size_t count;
} Buffers;

static Buffers buffers_new(unsigned pad)
{
    Buffers b = {NULL, NULL, JELLI_WIDTH + pad, (size_t)(JELLI_WIDTH + pad) * JELLI_HEIGHT};
    b.got = malloc(b.count * sizeof(uint16_t));
    b.want = malloc(b.count * sizeof(uint16_t));
    CHECK_CASE(b.got != NULL && b.want != NULL);
    return b;
}

static uint16_t background(const Buffers *b, uint32_t salt, size_t i)
{
    return i % b->stride >= JELLI_WIDTH ? (uint16_t)SENTINEL : (uint16_t)(i * 2654435761u ^ salt);
}

/* A busy background (so every blend shows) with the stride padding as sentinel. */
static uint32_t buffers_fill(Buffers *b, RefRandom *random)
{
    uint32_t salt = ref_next(random);
    for (size_t i = 0u; i < b->count; ++i)
        b->got[i] = b->want[i] = background(b, salt, i);
    return salt;
}

static void check_buffers(const Buffers *b)
{
    for (size_t i = 0u; i < b->count; ++i)
        if (b->got[i] != b->want[i]) {
            fprintf(stderr, "pixel %u,%u: got 0x%04x want 0x%04x\n", (unsigned)(i % b->stride),
                    (unsigned)(i / b->stride), b->got[i], b->want[i]);
            CHECK_CASE(b->got[i] == b->want[i]);
        }
}

typedef struct {
    int x, y, clip[4];
    unsigned scale;
    bool dim;
    uint8_t night;
} Draw;

/* The reference: every output pixel inside clip, surface and panel, sampled, tinted,
 * dimmed, then blended. Undrawable sprites leave the buffer alone. */
static void ref_draw(Buffers *b, const JelliAsset *asset, const Draw *d)
{
    if (!ref_supported(asset, d->scale) || !ref_readable(asset))
        return;
    for (unsigned oy = 0u; oy < asset->height * d->scale; ++oy)
        for (unsigned ox = 0u; ox < asset->width * d->scale; ++ox) {
            int px = d->x + (int)ox, py = d->y + (int)oy;
            if (px < d->clip[0] || py < d->clip[1] || px >= d->clip[2] || py >= d->clip[3] ||
                !ref_in_round(px, py))
                continue;
            uint16_t color = 0u;
            unsigned alpha = ref_sample(asset, d->scale, ox, oy, &color);
            if (!alpha)
                continue;
            if (d->night)
                color = jelli_pet_night_color(color, d->night);
            if (d->dim)
                color = (uint16_t)((color >> 1) & 0x7befu);
            uint16_t *pixel = &b->want[(size_t)py * b->stride + (size_t)px];
            *pixel = ref_blend(*pixel, color, alpha);
        }
}

static void draw_clipped(uint16_t *pixels, unsigned stride, const JelliAssetSet *set, const Draw *d,
                         const int clip[4])
{
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, stride, {0}};
    Canvas c = {&surface, clip[0], clip[1], clip[2], clip[3], d->night, d->dim, set};
    jelli_canvas_sprite(&c, set->items[0].id, d->x, d->y, d->scale);
}

static int coordinate(RefRandom *random, unsigned extent, unsigned size)
{
    return (int)ref_below(random, extent + 2u * size + 8u) - (int)size - 4;
}

static Draw random_draw(RefRandom *random, const JelliAsset *asset, unsigned scale)
{
    Draw d = {.scale = scale, .dim = ref_below(random, 3u) == 0u};
    d.night = ref_below(random, 3u) == 0u ? (uint8_t)(1u + ref_below(random, 255u)) : 0u;
    d.x = coordinate(random, JELLI_WIDTH, asset->width * scale);
    d.y = coordinate(random, JELLI_HEIGHT, asset->height * scale);
    for (unsigned axis = 0u; axis < 2u; ++axis) { /* Usually around the sprite, sometimes not. */
        int origin = axis ? d.y : d.x, size = (int)((axis ? asset->height : asset->width) * scale);
        int low = origin + (int)ref_below(random, (unsigned)size + 1u) - size / 2;
        int high = low + (int)ref_below(random, (unsigned)size * 2u + 2u);
        if (ref_below(random, 4u) == 0u) {
            low = 0;
            high = JELLI_WIDTH;
        }
        low = low < 0 ? 0 : low > JELLI_WIDTH ? JELLI_WIDTH : low;
        high = high < low ? low : high > JELLI_WIDTH ? JELLI_WIDTH : high;
        d.clip[axis] = low;
        d.clip[axis + 2u] = high;
    }
    return d;
}

/* One draw against the reference, then the same clip as four tiles over a fresh copy
 * of the background equals the single draw. */
static void check_draw(Buffers *b, const JelliAssetSet *set, RefRandom *random, unsigned *drawn)
{
    const JelliAsset *asset = &set->items[0];
    unsigned scale = 1u + ref_below(random, 16u);
    Draw d = random_draw(random, asset, scale);
    buffers_fill(b, random);
    draw_clipped(b->got, b->stride, set, &d, d.clip);
    ref_draw(b, asset, &d);
    check_buffers(b);
    *drawn += ref_supported(asset, scale) && ref_readable(asset) ? 1u : 0u;
    buffers_fill(b, random);
    draw_clipped(b->want, b->stride, set, &d, d.clip);
    int mx = d.clip[0] + (int)ref_below(random, (unsigned)(d.clip[2] - d.clip[0]) + 1u);
    int my = d.clip[1] + (int)ref_below(random, (unsigned)(d.clip[3] - d.clip[1]) + 1u);
    const int tiles[4][4] = {{d.clip[0], d.clip[1], mx, my},
                             {mx, d.clip[1], d.clip[2], my},
                             {d.clip[0], my, mx, d.clip[3]},
                             {mx, my, d.clip[2], d.clip[3]}};
    for (unsigned t = 0u; t < 4u; ++t)
        draw_clipped(b->got, b->stride, set, &d, tiles[t]);
    check_buffers(b);
}

static void random_draws_match_reference(void)
{
    Buffers surfaces[MAX_PAD + 1];
    for (unsigned pad = 0u; pad <= MAX_PAD; ++pad)
        surfaces[pad] = buffers_new(pad);
    unsigned drawn = 0u;
    for (unsigned i = 0u; i < DRAWS; ++i) {
        current_seed = 0xd7a90000u + i;
        RefRandom random = {current_seed * 2654435761u | 1u};
        RefSprite sprite;
        ref_sprite_random(&sprite, &random, 63000u, 1u + ref_below(&random, 24u), true);
        const JelliAssetSet set = {&sprite.asset, 1u, NULL};
        for (unsigned k = 0u; k < 4u; ++k)
            check_draw(&surfaces[ref_below(&random, MAX_PAD + 1u)], &set, &random, &drawn);
        ref_sprite_free(&sprite);
    }
    for (unsigned pad = 0u; pad <= MAX_PAD; ++pad) {
        free(surfaces[pad].got);
        free(surfaces[pad].want);
    }
    CHECK_CASE(drawn > 600u);
    printf("compositor: %u draws (+%u tiled), %u drawable, the rest refused\n", DRAWS * 4u,
           DRAWS * 4u, drawn);
}

/* The original 16-step particle fade, kept for opaque texels. */
static uint16_t fade16(uint16_t back, uint16_t front, unsigned fade)
{
    unsigned r = ((back >> 11) * (16u - fade) + (front >> 11) * fade) / 16u;
    unsigned g = (((back >> 5) & 63u) * (16u - fade) + ((front >> 5) & 63u) * fade) / 16u;
    unsigned b = ((back & 31u) * (16u - fade) + (front & 31u) * fade) / 16u;
    return (uint16_t)(r << 11 | g << 5 | b);
}

static void ref_magic(Buffers *b, const JelliAssetSet *set, const JelliParticle *p)
{
    unsigned scale = (p->style & 8u) ? 2u : 1u;
    const JelliAsset *asset = &set->items[p->style & 7u];
    if (!ref_supported(asset, scale) || !ref_readable(asset))
        return;
    unsigned width = asset->width * scale, height = asset->height * scale;
    unsigned fade = p->life < 8u ? p->life * 2u : 16u;
    int left = p->x / 16 - (int)(width / 2u), top = p->y / 16 - (int)(height / 2u);
    for (unsigned y = 0u; y < height; ++y)
        for (unsigned x = 0u; x < width; ++x) {
            int px = left + (int)x, py = top + (int)y;
            if (px < 0 || py < 0 || px >= JELLI_WIDTH || py >= JELLI_HEIGHT ||
                !ref_in_round(px, py))
                continue;
            uint16_t color = 0u;
            unsigned sample = ref_sample(asset, scale, x, y, &color);
            uint16_t *pixel = &b->want[(size_t)py * b->stride + (size_t)px];
            if (sample == 255u)
                *pixel = fade16(*pixel, color, fade);
            else if (sample * fade / 16u)
                *pixel = ref_blend(*pixel, color, sample * fade / 16u);
        }
}

/* Eight random 16x16 effects (densities incl. unsupported 5 and 7 at 2x, masks and
 * soft alpha), random magic particles on and off the panel. */
static unsigned particles_case(Buffers *b, RefRandom *random, RefSprite effects[8])
{
    static const uint8_t densities[] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 0u};
    JelliAsset items[8];
    for (unsigned e = 0u; e < 8u; ++e) {
        unsigned density = densities[ref_below(random, sizeof(densities))];
        ref_sprite_alpha(&effects[e], 9001u + e, 16u, 16u, density ? density : 1u);
        effects[e].asset.density = (uint8_t)density;
        size_t count = (size_t)256u * (density ? density * density : 1u);
        for (size_t i = 0u; i < count; ++i)
            effects[e].pixels[i] = (uint16_t)ref_next(random);
        ref_fill_alpha(effects[e].alpha, count, random);
        items[e] = effects[e].asset;
    }
    const JelliAssetSet set = {items, 8u, NULL};
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.assets = &set;
    for (unsigned i = 0u; i < JELLI_PARTICLE_CAPACITY; ++i)
        if (ref_below(random, 3u) != 0u)
            ui.particles.items[i] = (JelliParticle){
                (int16_t)(((int)ref_below(random, 560u) - 47) * 16 + (int)ref_below(random, 16u)),
                (int16_t)(((int)ref_below(random, 560u) - 47) * 16),
                0,
                0,
                (uint8_t)(1u + ref_below(random, 20u)),
                (uint8_t)(128u | ref_below(random, 16u))};
    ui.particles.changed = true;
    JelliRect damage = jelli_particles_bounds(&ui.particles);
    uint32_t salt = buffers_fill(b, random);
    JelliSurface surface = {
        b->got, JELLI_WIDTH, JELLI_HEIGHT, b->stride, {0u, 0u, JELLI_WIDTH, JELLI_HEIGHT}};
    jelli_pet_draw_particles(&surface, &game, &ui);
    for (unsigned i = 0u; i < JELLI_PARTICLE_CAPACITY; ++i)
        if (ui.particles.items[i].life)
            ref_magic(b, &set, &ui.particles.items[i]);
    check_buffers(b);
    unsigned changed = 0u;
    for (size_t i = 0u; i < b->count; ++i) { /* Painted pixels lie inside the damage. */
        unsigned x = (unsigned)(i % b->stride), y = (unsigned)(i / b->stride);
        if (b->got[i] == background(b, salt, i))
            continue;
        ++changed;
        CHECK_CASE(x >= damage.x && x < damage.x + damage.width && y >= damage.y &&
                   y < damage.y + damage.height);
    }
    return changed;
}

static void magic_particles_match_reference(void)
{
    Buffers b = buffers_new(3u);
    unsigned changed = 0u;
    for (unsigned i = 0u; i < 150u; ++i) {
        current_seed = 0x9a271c00u + i;
        RefRandom random = {current_seed * 2246822519u | 1u};
        RefSprite effects[8];
        changed += particles_case(&b, &random, effects);
        for (unsigned e = 0u; e < 8u; ++e)
            ref_sprite_free(&effects[e]);
    }
    free(b.got);
    free(b.want);
    CHECK_CASE(changed > 100000u);
    printf("particles: 150 scenes, %u pixels painted, all inside the reported damage\n", changed);
}

/* The shipped effects (density 6) drawn by real particles stay inside their damage. */
static void shipped_effects_stay_in_damage(void)
{
    current_seed = 0u;
    for (unsigned e = 0u; e < 8u; ++e)
        for (unsigned scale = 1u; scale <= 2u; ++scale) {
            const JelliAsset *asset = jelli_asset_find(9001u + e);
            JelliAssetSampler sampler;
            CHECK_CASE(asset && jelli_asset_sampler(&sampler, asset, scale));
            /* Bounds use radius 16: a sprite must fit in 33 x 33 around its centre. */
            CHECK_CASE(sampler.width <= 32u && sampler.height <= 32u);
        }
}

int main(void)
{
    random_draws_match_reference();
    magic_particles_match_reference();
    shipped_effects_stay_in_damage();
    puts("PASS: compositor and particles match the reference; tiles equal one draw");
    return 0;
}

#include "jelli/pet_ui.h"
#include "../core/pet_draw.h" /* Internal actor and particle passes under test. */
#include "hires_fixture.h"

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

enum { STRIDE = JELLI_WIDTH + 3, PIXELS = STRIDE * JELLI_HEIGHT, BACK = 0x2945 };
enum { STORED = 192, POOL = 1700000, CREATURE_COLOR = 0xfc73 };
static uint16_t pixels[PIXELS], fresh[PIXELS];
static uint16_t pool_pixels[POOL], creature_pixels[STORED * STORED];
static uint8_t pool_alpha[POOL], creature_alpha[STORED * STORED];
static JelliAsset fixtures[160], creatures[48];
static JelliAssetSet hires_set, creature_set;

/* Every creature ID any form can show; the actor test overrides all of them. */
static const uint32_t creature_ids[] = {1001u, 1002u, 1003u, 1004u, 1005u, 1006u, 1007u, 1008u,
                                        1009u, 1010u, 1011u, 1012u, 1021u, 1022u, 1023u, 1024u,
                                        1031u, 1032u, 1033u, 1034u, 1035u, 1036u, 1037u, 1038u,
                                        1041u, 1042u, 1043u, 1044u, 1045u, 1046u, 1047u, 1048u};

/* A 32x32 logical creature at density 6: an opaque stored rectangle x 60..131,
 * y 30..179 with two symmetric 1-pixel-wide faint columns (alpha 100, below the hit
 * threshold) at x 66 and 125, rows 60..89. Logical bounds 10,5,22,30; ground (16, 30). */
static void make_creature(void)
{
    uint64_t count = 0u, sum_y = 0u;
    for (unsigned y = 0u; y < STORED; ++y)
        for (unsigned x = 0u; x < STORED; ++x) {
            bool inside = x >= 60u && x < 132u && y >= 30u && y < 180u;
            bool faint = (x == 66u || x == 125u) && y >= 60u && y < 90u;
            creature_pixels[y * STORED + x] = inside ? CREATURE_COLOR : 0u;
            creature_alpha[y * STORED + x] = !inside ? 0u : faint ? 100u : 255u;
            if (inside && !faint) {
                ++count;
                sum_y += 2u * y + 1u;
            }
        }
    JelliAsset base = {.width = 32u,
                       .height = 32u,
                       .pixels = creature_pixels,
                       .ground_x_q8 = 4096u,
                       .ground_y_q8 = 7680u,
                       .centroid_x_q8 = 4096u,
                       .centroid_y_q8 = (uint16_t)((sum_y * 128u + count * 3u) / (count * 6u)),
                       .left = 10u,
                       .top = 5u,
                       .right = 22u,
                       .bottom = 30u,
                       .density = 6u,
                       .alpha = creature_alpha};
    for (size_t i = 0u; i < sizeof(creature_ids) / sizeof(creature_ids[0]); ++i) {
        creatures[i] = base;
        creatures[i].id = creature_ids[i];
    }
    creature_set = (JelliAssetSet){creatures, sizeof(creature_ids) / sizeof(creature_ids[0]), 0};
}

/* Logical layout is unchanged by density; touch samples the stored alpha. */
static void actor_layout_and_touch(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.assets = &creature_set;
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(ui.actor_frame && ui.actor_frame->alpha == creature_alpha);
    CHECK(ui.actor_x == 137 && ui.actor_y == 76);
    CHECK(ui.actor_bounds.x == 197u && ui.actor_bounds.y == 106u);
    CHECK(ui.actor_bounds.width == 72u && ui.actor_bounds.height == 150u);
    uint16_t opaque = jelli_pet_night_color(CREATURE_COLOR, ui.last_view.night);
    /* Drawn 1:1: the ground row is y 255 and the opaque body is centred on x 233. */
    for (unsigned y = 106u; y < 256u; ++y)
        CHECK(pixels[y * STRIDE + 199u] == opaque && pixels[y * STRIDE + 266u] == opaque);
    CHECK(pixels[255u * STRIDE + 233u] == opaque && pixels[256u * STRIDE + 233u] != opaque);
    CHECK(pixels[150u * STRIDE + 203u] != opaque && pixels[150u * STRIDE + 262u] != opaque);
    CHECK(!jelli_pet_touch_actor(&ui, &game, 203, 150)); /* Faint stored column. */
    CHECK(!jelli_pet_touch_actor(&ui, &game, 196, 150)); /* Clear, left of the body. */
    CHECK(!jelli_pet_touch_actor(&ui, &game, 262, 140));
    CHECK(jelli_pet_touch_actor(&ui, &game, 204, 150)); /* Next stored column is opaque. */
    CHECK(jelli_pet_touch_actor(&ui, &game, 203, 120)); /* Same column above the faint rows. */
    ui.menu_open = true;
    jelli_pet_render(&surface, &game, &ui, 20u, false);
    /* Rings centre the creature by its logical centroid. */
    CHECK(ui.actor_x == 137 &&
          ui.actor_y == 233 - (int)((ui.actor_frame->centroid_y_q8 * 6u + 128u) / 256u));
}

static void check_matches_full_redraw(const JelliGame *game, const JelliPetUi *ui, uint64_t time)
{
    JelliPetUi copy = *ui;
    copy.rendered = false;
    JelliSurface surface = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, game, &copy, time, false);
    for (size_t y = 0u; y < JELLI_HEIGHT; ++y)
        CHECK(memcmp(&pixels[y * STRIDE], &fresh[y * STRIDE], JELLI_WIDTH * sizeof(uint16_t)) == 0);
}

/* Renders `frames` 16 ms steps (particles advance as the engine does), checking every
 * partial redraw against a full one. */
static unsigned run(const JelliGame *game, JelliPetUi *ui, uint64_t *time, unsigned frames)
{
    unsigned partials = 0u;
    for (unsigned i = 0u; i < frames; ++i, *time += 16u) {
        jelli_particles_advance(&ui->particles, 16u);
        JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
        jelli_pet_render(&surface, game, ui, *time, false);
        if (surface.damage.width && surface.damage.width < JELLI_WIDTH) {
            ++partials;
            check_matches_full_redraw(game, ui, *time);
        }
    }
    return partials;
}

/* With every sprite kind at its production density, celebration particles, the
 * battery corner, rings, Settings and the collection repaint exactly like full frames. */
static void partial_redraws_equal_full(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    jelli_prize_complete(&game, JELLI_PRIZE_WASH); /* A bobbing present (2x) to catch. */
    ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 15u};
    ui.assets = &hires_set;
    uint64_t time = 0u;
    unsigned partials = run(&game, &ui, &time, 2u);
    CHECK(ui.actor_frame && ui.actor_frame->density == 6u);
    int x = (int)(ui.actor_bounds.x + ui.actor_bounds.width / 2u);
    int y = (int)(ui.actor_bounds.y + ui.actor_bounds.height / 2u);
    CHECK(jelli_pet_touch_actor(&ui, &game, x, y));
    partials += run(&game, &ui, &time, 120u);
    CHECK(partials >= 20u); /* Particle frames redraw only their region. */
    jelli_pet_ui_swipe(&ui, &game, 0, -80);
    run(&game, &ui, &time, 40u);
    ui.page = JELLI_UI_SETTINGS;
    run(&game, &ui, &time, 40u);
    ui.page = JELLI_UI_COLLECTION;
    run(&game, &ui, &time, 40u);
    ui.page = JELLI_UI_CARE;
    run(&game, &ui, &time, 40u);
}

/* Independent per-channel rounding blend. */
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

/* The original particle fade: truncating 16-step blend, kept for opaque texels. */
static uint16_t fade16(uint16_t back, uint16_t front, unsigned fade)
{
    unsigned r = ((back >> 11) * (16u - fade) + (front >> 11) * fade) / 16u;
    unsigned g = (((back >> 5) & 63u) * (16u - fade) + ((front >> 5) & 63u) * fade) / 16u;
    unsigned b = ((back & 31u) * (16u - fade) + (front & 31u) * fade) / 16u;
    return (uint16_t)(r << 11 | g << 5 | b);
}

/* Magic particles sample the same stored alpha and fade it by remaining life. */
static void particles_blend_sampled_alpha(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.assets = &hires_set;
    static const uint8_t lives[] = {16u, 4u};
    for (unsigned scaled = 0u; scaled < 2u; ++scaled)
        for (size_t l = 0u; l < sizeof(lives); ++l) {
            for (size_t i = 0u; i < PIXELS; ++i)
                pixels[i] = BACK;
            memset(&ui.particles, 0, sizeof(ui.particles));
            ui.particles.items[0] = (JelliParticle){
                233 * 16, 233 * 16, 0, 0, lives[l], (uint8_t)(128u | (scaled ? 8u : 0u))};
            ui.particles.changed = true;
            JelliSurface surface = {
                pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0u, 0u, JELLI_WIDTH, JELLI_HEIGHT}};
            jelli_pet_draw_particles(&surface, &game, &ui);
            JelliAssetSampler sampler;
            CHECK(jelli_asset_sampler(&sampler, jelli_asset_lookup(&hires_set, 9001u),
                                      scaled ? 2u : 1u));
            CHECK(sampler.down == (scaled ? 3u : 6u));
            unsigned fade = lives[l] < 8u ? lives[l] * 2u : 16u, drawn = 0u;
            for (unsigned sy = 0u; sy < sampler.height; ++sy)
                for (unsigned sx = 0u; sx < sampler.width; ++sx) {
                    uint16_t color = 0u;
                    unsigned sample = jelli_asset_sample(&sampler, sx, sy, &color);
                    unsigned alpha = sample * fade / 16u;
                    unsigned px = 233u - sampler.width / 2u + sx,
                             py = 233u - sampler.height / 2u + sy;
                    uint16_t want = sample == 255u ? fade16(BACK, color, fade)
                                    : alpha        ? ref_blend(BACK, color, alpha)
                                                   : BACK;
                    CHECK(pixels[py * STRIDE + px] == want);
                    if (alpha && sample < 255u)
                        ++drawn;
                }
            CHECK(drawn > 10u); /* Soft (partial-alpha) texels took the 0..255 path. */
        }
}

int main(void)
{
    HiresPool pool = {pool_pixels, pool_alpha, POOL, 0u};
    size_t count = hires_fixture_set(fixtures, sizeof(fixtures) / sizeof(fixtures[0]), &pool);
    CHECK(count >= 87u); /* Every kind but props, backgrounds and the font. */
    hires_set = (JelliAssetSet){fixtures, count, NULL};
    make_creature();
    actor_layout_and_touch();
    particles_blend_sampled_alpha();
    partial_redraws_equal_full();
    puts("PASS: logical layout, stored-alpha touch, particles, partial == full at density");
    return 0;
}

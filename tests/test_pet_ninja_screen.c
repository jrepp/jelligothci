/* The ninja's home screen: its NIRMAL heading, every frame drawn, partial redraws
 * of the battery indicator and stat tile over every ninja state, touch hit-testing
 * on its alpha, and its ring and activity layouts on the round panel. */
#include "jelli/assets.h"
#include "../core/pet_battery.h" /* Internal battery rect under test. */
#include "../core/pet_draw.h"    /* Internal actor layout under test. */

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

enum { STRIDE = JELLI_WIDTH + 5, PIXELS = STRIDE * JELLI_HEIGHT, POSES = 8 };
static uint16_t pixels[PIXELS], comparison[PIXELS], before[PIXELS];

/* Ninja asset IDs in pose order, transcribed from assets/slice/assets.json. */
static const uint32_t ninja_ids[POSES] = {1031u, 1032u, 1033u, 1034u, 1035u, 1036u, 1037u, 1038u};

static int ninja_pose(uint32_t id)
{
    for (int pose = 0; pose < POSES; ++pose)
        if (ninja_ids[pose] == id)
            return pose;
    return -1;
}

static JelliSurface surface_for(uint16_t *buffer)
{
    return (JelliSurface){buffer, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
}

/* The ninja, active, in one of the six states its care and health can produce. */
static void ninja_in_state(JelliGame *game, unsigned state)
{
    jelli_game_init(game);
    game->active = 2u;
    JelliPet *pet = &game->pets[2];
    if (state == 1u || state == 2u) {
        pet->activity = state == 1u ? JELLI_EATING : JELLI_PLAYING;
        pet->interaction_due = 50u;
    } else if (state == 3u) {
        pet->asleep = true;
        pet->nap_due = 36000u;
    } else if (state == 4u) {
        pet->health = JELLI_UNWELL;
    } else if (state == 5u) {
        pet->reaction = 1u;
    }
    CHECK(jelli_game_valid(game));
}

static bool opaque(const JelliAsset *a, unsigned column, unsigned row)
{
    return column < a->width && row < a->height &&
           (a->mask[row * a->mask_stride + column / 8u] & (0x80u >> (column % 8u))) != 0u;
}

/* The heading names the active pet: NIRMAL differs from both jelly forms' headings. */
static void ninja_heading(void)
{
    static JelliPetUi ui;
    JelliGame game;
    ninja_in_state(&game, 0u);
    jelli_pet_ui_init(&ui);
    JelliSurface s = surface_for(pixels), other = surface_for(comparison);
    jelli_pet_render(&s, &game, &ui, 0u, false);
    for (uint8_t form = JELLI_FORM_MINT; form <= JELLI_FORM_LILAC; ++form) {
        game.pets[2].form = form;
        jelli_pet_ui_init(&ui);
        jelli_pet_render(&other, &game, &ui, 0u, false);
        bool differs = false;
        for (unsigned y = 16u; y < 52u; ++y)
            differs =
                differs || memcmp(pixels + (size_t)y * STRIDE, comparison + (size_t)y * STRIDE,
                                  JELLI_WIDTH * sizeof(uint16_t)) != 0;
        CHECK(differs);
    }
    /* Defensive fallback: a form past the table is titled like Mint, never read past it. */
    game.pets[2].form = JELLI_FORM_MINT;
    jelli_pet_ui_init(&ui);
    jelli_pet_render(&other, &game, &ui, 0u, false);
    JelliPetRenderKey unknown = ui.last_view;
    unknown.form = 200u;
    jelli_pet_draw(&s, &game, &ui, &unknown);
    for (unsigned y = 16u; y < 52u; ++y)
        CHECK(memcmp(pixels + (size_t)y * STRIDE, comparison + (size_t)y * STRIDE,
                     JELLI_WIDTH * sizeof(uint16_t)) == 0);
}

/* Pixel-level: the heading spells each form's name (MINT, LILAC, NIRMAL) in white 3x
 * glyphs centred at x = 233, and every glyph and shadow pixel lies on the round panel.
 * No other white pixel appears in the heading band. */
static void heading_spells_each_name(void)
{
    static const char *const names[JELLI_FORM_COUNT] = {"MINT", "LILAC", "NIRMAL"};
    for (uint8_t form = 0u; form < JELLI_FORM_COUNT; ++form) {
        static JelliPetUi ui;
        JelliGame game;
        ninja_in_state(&game, 0u);
        game.pets[2].form = form;
        jelli_pet_ui_init(&ui);
        JelliSurface s = surface_for(pixels);
        jelli_pet_render(&s, &game, &ui, 0u, false);
        const char *name = names[form];
        size_t length = strlen(name);
        int left = 233 - (int)(length * 24u) / 2;
        CHECK(left > 0 && left + (int)(length * 24u) + 2 < (int)JELLI_WIDTH);
        unsigned lit = 0u;
        for (unsigned i = 0u; i < (unsigned)length; ++i) {
            const uint8_t *rows = jelli_asset_lookup_glyph(ui.assets, (uint8_t)name[i]);
            CHECK(rows != NULL);
            for (unsigned row = 0u; row < 12u; ++row)
                for (unsigned column = 0u; column < 8u; ++column) {
                    if (!(rows[row] & (0x80u >> column)))
                        continue;
                    for (unsigned d = 0u; d < 9u; ++d) {
                        int x = left + (int)(i * 24u + column * 3u + d % 3u);
                        int y = 16 + (int)(row * 3u + d / 3u);
                        CHECK(jelli_canvas_in_round(x, y) && jelli_canvas_in_round(x + 2, y + 2));
                        CHECK(pixels[(size_t)y * STRIDE + (size_t)x] == 0xffffu);
                        ++lit;
                    }
                }
        }
        unsigned white = 0u;
        for (unsigned y = 16u; y < 16u + 36u; ++y)
            for (unsigned x = 0u; x < JELLI_WIDTH; ++x)
                white += pixels[y * STRIDE + x] == 0xffffu ? 1u : 0u;
        CHECK(lit > 200u && white == lit); /* Exactly this name, nothing else. */
    }
}

/* Every ninja frame is drawn: the opaque blocks clear of captions and particles carry
 * the asset's own colours, and the idle cycle reaches all four idle frames. */
static void ninja_home_frames_drawn(void)
{
    static const uint32_t expected[] = {1031u, 1033u, 1034u, 1035u, 1036u, 1034u};
    for (unsigned state = 0u; state < 6u; ++state) {
        JelliGame game;
        static JelliPetUi ui;
        ninja_in_state(&game, state);
        jelli_pet_ui_init(&ui);
        JelliSurface s = surface_for(pixels);
        jelli_pet_render(&s, &game, &ui, 0u, false);
        const JelliAsset *a = ui.actor_frame;
        CHECK(a && a->id == expected[state] && ui.last_view.form == JELLI_FORM_NINJA);
        unsigned blocks = 0u, matching = 0u;
        for (unsigned row = 0u; row < a->height; ++row) {
            for (unsigned column = 0u; column < a->width; ++column) {
                int x = ui.actor_x + (int)column * 6 + 3, y = ui.actor_y + (int)row * 6 + 3;
                if (!opaque(a, column, row) || y >= 240 || x >= 300)
                    continue;
                ++blocks;
                matching += pixels[y * STRIDE + x] == a->pixels[row * a->width + column];
            }
        }
        CHECK(blocks > 0u && matching * 20u >= blocks * 19u);
    }
    JelliGame game;
    static JelliPetUi ui;
    ninja_in_state(&game, 0u);
    jelli_pet_ui_init(&ui);
    bool seen[POSES] = {false};
    JelliSurface s = surface_for(pixels);
    for (uint64_t t = 0u; t < UINT64_C(64) * 900u; t += 150u) {
        jelli_pet_render(&s, &game, &ui, t, false);
        int pose = ninja_pose(ui.actor_frame->id);
        CHECK(pose >= 0);
        seen[pose] = true;
    }
    CHECK(seen[0] && seen[1] && seen[6] && seen[7]);
}

static bool inside(JelliRect r, unsigned x, unsigned y)
{
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

/* One incremental frame: only damaged pixels change, and the result equals a full
 * repaint of the same state. Returns the damage rectangle. */
static JelliRect redraw_matches_full(const JelliGame *game, JelliPetUi *ui)
{
    static JelliPetUi fresh;
    JelliSurface s = surface_for(pixels), full = surface_for(comparison);
    memcpy(before, pixels, sizeof(before));
    jelli_pet_render(&s, game, ui, 0u, false);
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
        for (unsigned x = 0u; x < JELLI_WIDTH; ++x)
            if (pixels[y * STRIDE + x] != before[y * STRIDE + x])
                CHECK(inside(s.damage, x, y));
    fresh = *ui;
    fresh.rendered = false;
    jelli_pet_render(&full, game, &fresh, 0u, false);
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
        CHECK(memcmp(pixels + (size_t)y * STRIDE, comparison + (size_t)y * STRIDE,
                     JELLI_WIDTH * sizeof(uint16_t)) == 0);
    return s.damage;
}

/* Battery-only and tile-only redraws over every ninja state: each partial repaint
 * leaves exactly the pixels a full frame would, whatever the ninja's pose. */
static void ninja_partial_redraws(void)
{
    static const JelliPowerReading readings[] = {
        {JELLI_POWER_NO_BATTERY, 0u},  {JELLI_POWER_DISCHARGING, 15u},
        {JELLI_POWER_DISCHARGING, 9u}, {JELLI_POWER_CHARGING, 50u},
        {JELLI_POWER_UNKNOWN, 0u},     {JELLI_POWER_DISCHARGING, 100u}};
    const JelliRect battery = jelli_pet_battery_bounds();
    unsigned battery_only = 0u, tile_only = 0u;
    for (unsigned state = 0u; state < 6u; ++state) {
        JelliGame game;
        static JelliPetUi ui;
        ninja_in_state(&game, state);
        jelli_pet_ui_init(&ui);
        ui.power = readings[5];
        JelliSurface s = surface_for(pixels);
        jelli_pet_render(&s, &game, &ui, 0u, false);
        for (unsigned r = 0u; r < sizeof(readings) / sizeof(readings[0]); ++r) {
            ui.power = readings[r];
            JelliRect damage = redraw_matches_full(&game, &ui);
            /* Sleep particles may widen it; it always covers the whole battery rect. */
            CHECK(inside(damage, battery.x, battery.y) &&
                  inside(damage, battery.x + battery.width - 1u, battery.y + battery.height - 1u));
            battery_only += damage.width < JELLI_WIDTH ? 1u : 0u;
        }
        game.pets[2].needs[JELLI_SATIETY] = 400u;
        JelliRect damage = redraw_matches_full(&game, &ui);
        tile_only += damage.y == 282u && damage.height == 100u ? 1u : 0u;
    }
    CHECK(battery_only >= 6u * 5u && tile_only > 0u);
}

/* Touch lands only on the ninja's opaque pixels, including its hood and headband
 * tail, never on transparent corners of its 6x frame. */
static void ninja_touch_uses_alpha(void)
{
    for (unsigned state = 0u; state < 6u; ++state) {
        JelliGame game;
        static JelliPetUi ui;
        ninja_in_state(&game, state);
        jelli_pet_ui_init(&ui);
        JelliSurface s = surface_for(pixels);
        jelli_pet_render(&s, &game, &ui, 0u, false);
        const JelliAsset *a = ui.actor_frame;
        CHECK(a && ninja_pose(a->id) >= 0 && !ui.menu_open);
        unsigned hits = 0u, misses = 0u;
        for (int y = ui.actor_y - 6; y < ui.actor_y + 32 * 6 + 6; y += 3) {
            for (int x = ui.actor_x - 6; x < ui.actor_x + 32 * 6 + 6; x += 3) {
                bool solid =
                    x >= ui.actor_x && y >= ui.actor_y &&
                    opaque(a, (unsigned)(x - ui.actor_x) / 6u, (unsigned)(y - ui.actor_y) / 6u);
                bool hit = jelli_pet_touch_actor(&ui, &game, x, y);
                CHECK(hit == solid);
                hits += hit ? 1u : 0u;
                misses += !hit && inside(ui.actor_bounds, (unsigned)x, (unsigned)y) ? 1u : 0u;
            }
        }
        CHECK(hits > 0u && misses > 0u && jelli_game_valid(&game));
        CHECK(!jelli_pet_touch_actor(&ui, &game, 2147483647, ui.actor_y + 100));
        CHECK(!jelli_pet_touch_actor(&ui, &game, ui.actor_x + 100, 2147483647));
    }
}

/* Ring and activity layouts keep every ninja frame on the round panel. */
static void ninja_layouts_on_panel(void)
{
    static const JelliPetPage pages[] = {JELLI_UI_HOME, JELLI_UI_CARE, JELLI_UI_BRUSH};
    for (unsigned p = 0u; p < 3u; ++p) {
        for (unsigned pose = 0u; pose < POSES; ++pose) {
            JelliPetUi ui;
            jelli_pet_ui_init(&ui);
            JelliPetRenderKey view = {.form = JELLI_FORM_NINJA,
                                      .menu_open = pages[p] != JELLI_UI_HOME,
                                      .page = (uint8_t)pages[p]};
            ui.actor_frame = jelli_asset_find(ninja_ids[pose]);
            jelli_pet_actor_layout(&ui, &view);
            JelliRect b = ui.actor_bounds;
            CHECK(b.width > 0u && b.x + b.width <= JELLI_WIDTH && b.y + b.height <= JELLI_HEIGHT);
            /* Below both heading lines ("NIRMAL" and "@ HOME" end at y = 81). */
            CHECK(b.y >= 81u);
        }
    }
}

int main(void)
{
    ninja_heading();
    heading_spells_each_name();
    ninja_home_frames_drawn();
    ninja_partial_redraws();
    ninja_touch_uses_alpha();
    ninja_layouts_on_panel();
    puts("PASS: ninja heading names, home frames, partial redraw equality, alpha hit-testing, "
         "layouts");
    return 0;
}

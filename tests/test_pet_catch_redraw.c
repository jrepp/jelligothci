#include "jelli/pet_ui.h"

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

enum { STRIDE = JELLI_WIDTH + 3, PIXELS = STRIDE * JELLI_HEIGHT };
static uint16_t pixels[PIXELS], previous[PIXELS], fresh[PIXELS];
/* The present's fixed redraw rectangle (core/pet_gallery.c, jelli_pet_gallery_catch_bounds). */
static const JelliRect catch_rect = {194u, 66u, 79u, 84u};

static bool same_rect(JelliRect a, JelliRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static bool inside(JelliRect rect, unsigned x, unsigned y)
{
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

/* A partial update must leave exactly the pixels a full redraw would produce. */
static void check_matches_full_redraw(const JelliGame *game, const JelliPetUi *ui, uint64_t time)
{
    JelliPetUi copy = *ui;
    copy.rendered = false;
    JelliSurface surface = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, game, &copy, time, false);
    for (size_t y = 0u; y < JELLI_HEIGHT; ++y)
        CHECK(memcmp(&pixels[y * STRIDE], &fresh[y * STRIDE], JELLI_WIDTH * sizeof(uint16_t)) == 0);
}

static void offered_game(JelliGame *game, uint8_t slot, uint8_t form, bool asleep)
{
    jelli_game_init(game);
    game->active = slot;
    game->pets[slot].form = form;
    if (asleep) {
        game->pets[slot].asleep = true;
        game->pets[slot].nap_due = 360000u;
    }
    jelli_prize_complete(game, JELLI_PRIZE_WASH);
    CHECK(game->prizes.offered == 6u && jelli_game_valid(game)); /* Wash offers present 6. */
}

/* Bob steps repaint only the present's rectangle, identical to a full redraw. */
static void bob_redraws_only_the_present(void)
{
    static const JelliPowerReading readings[] = {{JELLI_POWER_NO_BATTERY, 0u},
                                                 {JELLI_POWER_DISCHARGING, 15u}};
    for (uint8_t form = 0u; form < JELLI_FORM_COUNT; ++form)
        for (unsigned r = 0u; r < 2u; ++r)
            for (unsigned display = 0u; display < 2u; ++display)
                for (unsigned asleep = 0u; asleep < 2u; ++asleep) {
                    JelliGame game;
                    offered_game(&game, 2u, form, asleep != 0u);
                    game.battery_display = display != 0u;
                    JelliPetUi ui;
                    jelli_pet_ui_init(&ui);
                    ui.power = readings[r];
                    unsigned catches = 0u, partials = 0u, fulls = 0u;
                    for (uint64_t time = 0u; time < 10000u; time += 16u) {
                        memcpy(previous, pixels, sizeof(pixels));
                        JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
                        jelli_pet_render(&surface, &game, &ui, time, false);
                        if (time == 0u || surface.damage.width == 0u)
                            continue;
                        if (surface.damage.width == JELLI_WIDTH) {
                            ++fulls;
                            continue;
                        }
                        ++partials;
                        check_matches_full_redraw(&game, &ui, time);
                        if (!same_rect(surface.damage, catch_rect))
                            continue; /* Sleep particles widen the damage while asleep. */
                        ++catches;
                        for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
                            for (unsigned x = 0u; x < STRIDE; ++x)
                                if (!inside(catch_rect, x, y) || x >= JELLI_WIDTH)
                                    CHECK(pixels[y * STRIDE + x] == previous[y * STRIDE + x]);
                    }
                    /* About 5.5 bob steps per second for 10 s; only pose changes repaint fully. */
                    CHECK(asleep ? partials >= 40u : catches >= 40u);
                    CHECK(fulls <= 20u);
                }
}

/* Every present at every bob height, by day and night, at home and in the garden: the
 * changed pixels stay inside the rectangle and reach its top and bottom rows. */
static void every_present_fits_the_rectangle(void)
{
    for (unsigned prize = 1u; prize <= JELLI_PRIZE_COUNT; ++prize)
        for (unsigned scene = 0u; scene < 4u; ++scene) {
            JelliGame game;
            jelli_game_init(&game);
            game.pets[0].location = (uint8_t)(scene & 1u);
            game.pets[0].phase_offset = scene & 2u ? JELLI_DAY_TICKS / 24u * 23u : 0u;
            game.prizes.offered = (uint8_t)prize;
            game.prizes.offered_pet = game.pets[0].id;
            CHECK(jelli_game_valid(&game));
            JelliPetRenderKey base = {0};
            unsigned top = JELLI_HEIGHT, bottom = 0u;
            for (unsigned phase = 0u; phase < 4u; ++phase) {
                JelliPetUi ui;
                jelli_pet_ui_init(&ui);
                JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
                jelli_pet_render(&surface, &game, &ui, 20u + phase * 180u, false);
                CHECK(ui.last_view.catch_phase == phase && ui.last_view.offered_prize == prize);
                if (phase == 0u) {
                    memcpy(previous, pixels, sizeof(pixels));
                    base = ui.last_view;
                    continue;
                }
                /* Only the bob differs: every time-driven field holds still. */
                CHECK(base.phase == ui.last_view.phase && base.night == ui.last_view.night);
                CHECK(base.ring_visible == ui.last_view.ring_visible &&
                      base.reward_active == ui.last_view.reward_active);
                for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
                    for (unsigned x = 0u; x < JELLI_WIDTH; ++x)
                        if (pixels[y * STRIDE + x] != previous[y * STRIDE + x]) {
                            CHECK(inside(catch_rect, x, y));
                            top = y < top ? y : top;
                            bottom = y > bottom ? y : bottom;
                        }
            }
            CHECK(top == catch_rect.y && bottom == catch_rect.y + catch_rect.height - 1u);
        }
}

/* Regression: a stat swipe at night repainted the tile without the night icon tint that
 * a full redraw applies (jelli_pet_draw_tile built its canvas with icon_night 0). */
static void night_tile_swipe_matches_full_redraw(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].phase_offset = JELLI_DAY_TICKS / 24u * 23u;
    CHECK(jelli_game_valid(&game));
    JelliPetUi ui;
    jelli_pet_ui_init(&ui);
    uint64_t time = 0u;
    for (; time <= 41000u; time += time < 40000u ? 1000u : 16u) { /* Let the night settle. */
        JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
        jelli_pet_render(&surface, &game, &ui, time, false);
    }
    CHECK(ui.last_view.night > 0u);
    for (unsigned swipe = 0u; swipe < JELLI_PET_STAT_COUNT; ++swipe, time += 16u) {
        jelli_pet_ui_swipe(&ui, &game, -80, 0);
        JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
        jelli_pet_render(&surface, &game, &ui, time, false);
        CHECK(same_rect(surface.damage, (JelliRect){90u, 282u, 286u, 100u})); /* Tile only. */
        check_matches_full_redraw(&game, &ui, time);
    }
}

int main(void)
{
    bob_redraws_only_the_present();
    every_present_fits_the_rectangle();
    night_tile_swipe_matches_full_redraw();
    puts("Catch redraw checks passed");
    return 0;
}

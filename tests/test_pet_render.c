#include "jelli/pet_ui.h"
#include "jelli/assets.h"

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

enum { STRIDE = JELLI_WIDTH + 5, PIXELS = STRIDE * JELLI_HEIGHT };
static uint16_t pixels[PIXELS];
static uint16_t comparison[PIXELS];

static void test_render(JelliGame *game, JelliPetUi *ui)
{
    memset(pixels, 0xa5, sizeof(pixels));
    JelliSurface surface = {
        .pixels = pixels, .width = JELLI_WIDTH, .height = JELLI_HEIGHT, .stride = STRIDE};
    jelli_pet_render(&surface, game, ui, 0u, false);
    CHECK(surface.damage.width == JELLI_WIDTH && surface.damage.height == JELLI_HEIGHT);
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y) {
        for (unsigned x = JELLI_WIDTH; x < STRIDE; ++x)
            CHECK(pixels[y * STRIDE + x] == 0xa5a5u);
    }
    jelli_pet_render(&surface, game, ui, 100u, false);
    CHECK(surface.damage.width == 0u && surface.damage.height == 0u);
    game->pets[game->active].ticks += 1u;
    jelli_pet_render(&surface, game, ui, 101u, false);
    CHECK(surface.damage.width == 0u && surface.damage.height == 0u);
    game->pets[game->active].needs[JELLI_SATIETY] = 400u;
    jelli_pet_render(&surface, game, ui, 102u, false);
    CHECK(surface.damage.width == 286u && surface.damage.height == 100u);
    jelli_pet_render(&surface, game, ui, 900u, false);
    CHECK(surface.damage.width == JELLI_WIDTH && surface.damage.height == JELLI_HEIGHT);
}

static bool region_changed(unsigned left, unsigned top, unsigned width, unsigned height)
{
    for (unsigned y = top; y < top + height; ++y)
        for (unsigned x = left; x < left + width; ++x)
            if (pixels[y * STRIDE + x] != comparison[y * STRIDE + x])
                return true;
    return false;
}

static void test_pet_feedback(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {
        .pixels = pixels, .width = JELLI_WIDTH, .height = JELLI_HEIGHT, .stride = STRIDE};
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    memcpy(comparison, pixels, sizeof(comparison));
    JelliCommand gift = {JELLI_CMD_GIFT, game.pets[game.active].id, 0u};
    CHECK(jelli_game_command(&game, gift) == JELLI_OK);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(region_changed(137u, 82u, 192u, 192u));

    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    memcpy(comparison, pixels, sizeof(comparison));
    JelliCommand care = {JELLI_CMD_CARE, game.pets[game.active].id, 0u};
    CHECK(jelli_game_command(&game, care) == JELLI_OK);
    game.pets[game.active].health = JELLI_RECOVERING;
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(region_changed(0u, 254u, JELLI_WIDTH, 24u));
}

static void test_page_items(void)
{
    for (unsigned page = 0u; page < 6u; ++page) {
        for (unsigned item = 0u; item < 6u; ++item) {
            JelliPetUiItem button = jelli_pet_ui_item((JelliPetPage)page, item, false);
            JelliPetUiButton bounds;
            CHECK(jelli_pet_ui_button((JelliPetPage)page, item + 1u, false, true, &bounds) ==
                  (button.label[0] != '\0'));
        }
    }
    CHECK(jelli_pet_ui_item(JELLI_UI_CARE, 2u, true).action == JELLI_UI_ACTION_CLEAN_WAKE);
    CHECK(strcmp(jelli_pet_ui_item(JELLI_UI_CARE, 2u, true).label, "CLEAN") == 0);
    CHECK(jelli_pet_ui_item(JELLI_UI_SETTINGS, 1u, false).action == JELLI_UI_ACTION_SWITCH_PET);
    CHECK(jelli_pet_ui_item(JELLI_UI_SETTINGS, 0u, false).action == JELLI_UI_ACTION_BEDTIME);
}

static void tap_item(JelliPetUi *ui, JelliGame *game, unsigned item)
{
    if (!ui->menu_open)
        jelli_pet_ui_tap(ui, game, 233, 420);
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_button(ui->page, item + 1u, game->pets[game->active].asleep, true, &button));
    jelli_pet_ui_tap(ui, game, (int)(button.bounds.x + 48u), (int)(button.bounds.y + 48u));
}

static void test_navigation_and_actions(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    tap_item(&ui, &game, 0u);
    CHECK(ui.page == JELLI_UI_CARE);
    tap_item(&ui, &game, 2u);
    CHECK(game.pets[game.active].activity == JELLI_CLEANING);
    for (unsigned step = 0u; step < 10u; ++step)
        jelli_game_advance(&game, 1000u);
    CHECK(game.pets[game.active].activity == JELLI_IDLE);
    jelli_pet_ui_tap(&ui, &game, 233, 420);
    CHECK(ui.page == JELLI_UI_HOME);
    tap_item(&ui, &game, 4u);
    CHECK(ui.page == JELLI_UI_MORE);
    tap_item(&ui, &game, 0u);
    CHECK(game.pets[game.active].activity == JELLI_GIVING);
    CHECK(ui.save_requested);

    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    tap_item(&ui, &game, 5u);
    tap_item(&ui, &game, 1u);
    CHECK(ui.page == JELLI_UI_SETTINGS);
    CHECK(game.active == 1u);
    CHECK(ui.save_requested);

    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    tap_item(&ui, &game, 5u);
    CHECK(ui.page == JELLI_UI_SETTINGS);
    uint32_t bedtime = game.pets[game.active].bedtime;
    tap_item(&ui, &game, 0u);
    CHECK(game.pets[game.active].bedtime == (bedtime + 1u) % 24u);
    CHECK(ui.save_status == JELLI_SAVE_PENDING && ui.save_requested);

    game.resuming = true;
    game.resume_remaining_ms = 60000u;
    uint8_t page = (uint8_t)ui.page;
    jelli_pet_ui_tap(&ui, &game, 233, 420);
    CHECK((uint8_t)ui.page == page);
    CHECK(game.pets[game.active].bedtime == (bedtime + 1u) % 24u);
}

static void test_ring_moments_and_meter(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    jelli_pet_ui_tap(&ui, &game, 110, 111);
    CHECK(!ui.menu_open && ui.page == JELLI_UI_HOME);
    jelli_pet_ui_tap(&ui, &game, 233, 420);
    CHECK(ui.menu_open);
    tap_item(&ui, &game, 1u);
    CHECK(ui.page == JELLI_UI_MOMENTS);
    CHECK(jelli_pet_moment(&game.pets[0]) == 0u);
    ui.clock_known = true;
    ui.clock_minute = 20u * 60u;
    CHECK(jelli_pet_suggested_moment(&game.pets[0], &ui) == 3u);
    ui.clock_known = false;
    game.pets[0].phase_offset = 11u * 36000u;
    CHECK(jelli_pet_moment(&game.pets[0]) == 1u);
    game.pets[0].phase_offset = 15u * 36000u;
    CHECK(jelli_pet_moment(&game.pets[0]) == 2u);
    game.pets[0].phase_offset = 19u * 36000u;
    CHECK(jelli_pet_moment(&game.pets[0]) == 3u);
    tap_item(&ui, &game, 2u);
    CHECK(game.pets[0].location == 1u);
    CHECK(game.pets[0].activity == JELLI_PLAYING);
    for (unsigned i = 0; i < 10u; ++i)
        jelli_game_advance(&game, 800u);
    tap_item(&ui, &game, 3u);
    CHECK(game.pets[0].activity == JELLI_PLAYING);
    jelli_pet_ui_tap(&ui, &game, 233, 420);
    CHECK(ui.menu_open && ui.page == JELLI_UI_HOME);
    jelli_pet_ui_tap(&ui, &game, 233, 420);
    CHECK(!ui.menu_open);
    jelli_pet_ui_tap(&ui, &game, 233, 332);
    CHECK(ui.stat_offset == 0u);
    CHECK(jelli_pet_stat_score(0u) == 1u);
    CHECK(jelli_pet_stat_score(500u) == 50u);
    CHECK(jelli_pet_stat_score(1000u) == 100u);
    CHECK(jelli_pet_stat_score(UINT16_MAX) == 100u);
}

static void test_slide_damage(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, &game, &ui, 9900u, false);
    memcpy(comparison, pixels, sizeof(comparison));
    jelli_pet_render(&surface, &game, &ui, 10050u, false);
    CHECK(surface.damage.width == 0u);
    CHECK(ui.last_view.tile_phase == 0u);
    CHECK(!region_changed(0u, 0u, JELLI_WIDTH, 282u));
    CHECK(!region_changed(0u, 382u, JELLI_WIDTH, JELLI_HEIGHT - 382u));
    JelliPetUi fresh = ui;
    fresh.rendered = false;
    JelliSurface full = {comparison, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&full, &game, &fresh, 10050u, false);
    CHECK(memcmp(comparison, pixels, sizeof(pixels)) == 0);
    jelli_pet_render(&surface, &game, &ui, 10800u, false);
    CHECK(ui.last_view.stat_index == 0u && ui.last_view.tile_phase == 0u);
}

static void test_particles(void)
{
    JelliParticles a = {0}, b = {0};
    jelli_particles_burst(&a, 233, 233, true);
    b = a;
    CHECK(jelli_particles_count(&a) == 8u);
    jelli_particles_advance(&a, 137u);
    jelli_particles_advance(&b, 53u);
    jelli_particles_advance(&b, 84u);
    CHECK(memcmp(a.items, b.items, sizeof(a.items)) == 0);
    CHECK(a.remainder_ms == b.remainder_ms);
    for (unsigned i = 0; i < 10u; ++i)
        jelli_particles_burst(&a, 0, 0, true);
    CHECK(jelli_particles_count(&a) == JELLI_PARTICLE_CAPACITY);
    jelli_particles_advance(&a, UINT64_MAX);
    CHECK(jelli_particles_count(&a) == 0u);
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    memcpy(comparison, pixels, sizeof(pixels));
    jelli_particles_burst(&ui.particles, 233, 233, true);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(surface.damage.width <= 33u);
    CHECK(region_changed(225u, 225u, 16u, 16u));
    for (unsigned i = 0; i < 32u; ++i) {
        jelli_particles_advance(&ui.particles, 20u);
        jelli_pet_render(&surface, &game, &ui, 0u, false);
        CHECK(surface.damage.width < JELLI_WIDTH);
    }
    CHECK(jelli_particles_count(&ui.particles) == 0u);
    CHECK(memcmp(comparison, pixels, sizeof(pixels)) == 0);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(surface.damage.width == 0u);
}

static void test_manual_stat_timing(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, &game, &ui, 10350u, false);
    CHECK(ui.last_view.tile_phase == 0u);
    jelli_pet_ui_tap(&ui, &game, 233, 332);
    jelli_pet_render(&surface, &game, &ui, 10360u, false);
    CHECK(ui.last_view.stat_index == 0u && ui.last_view.tile_phase == 0u);
    jelli_pet_render(&surface, &game, &ui, 18000u, false);
    CHECK(ui.last_view.stat_index == 0u && ui.last_view.tile_phase == 0u);
    jelli_pet_ui_tap(&ui, &game, 233, 332);
    jelli_pet_ui_tap(&ui, &game, 233, 332);
    jelli_pet_render(&surface, &game, &ui, 18010u, false);
    CHECK(ui.last_view.stat_index == 0u && ui.last_view.tile_phase == 0u);
    CHECK(jelli_tunable_set(&ui.tunables, game.pets[0].id, JELLI_TUNE_IDLE_MS, 1800u));
    jelli_pet_render(&surface, &game, &ui, 18020u, false);
    CHECK(ui.last_view.phase == 0u && ui.last_view.stat_index == 0u);
    jelli_pet_render(&surface, &game, &ui, 19819u, false);
    CHECK(ui.last_view.phase == 0u);
    jelli_pet_render(&surface, &game, &ui, 19820u, false);
    CHECK(ui.last_view.phase == 1u);
    jelli_pet_render(&surface, &game, &ui, UINT64_MAX, false);
    jelli_pet_render(&surface, &game, &ui, 10u, false);
    CHECK(ui.last_view.tile_phase == 0u && ui.last_view.phase == 0u);
}

static void test_grounded_poses(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    for (unsigned form = 0; form < 2u; ++form) {
        for (unsigned pose = 0; pose < 6u; ++pose) {
            JelliPet *pet = &game.pets[0];
            pet->form = (uint8_t)form;
            pet->asleep = pose == 4u;
            pet->nap_due = pose == 4u ? 36000u : 0u;
            pet->health = pose == 5u ? JELLI_UNWELL : JELLI_WELL;
            pet->activity = pose == 2u ? JELLI_EATING : pose == 3u ? JELLI_PLAYING : JELLI_IDLE;
            ui.rendered = false;
            jelli_pet_render(&surface, &game, &ui, pose == 1u ? 900u : 0u, false);
            unsigned count = 0, bottom = 0, center_sum = 0;
            for (unsigned y = 238u; y < 258u; ++y) {
                for (unsigned x = 90u; x < 376u; ++x) {
                    const JelliAsset *frame = ui.actor_frame;
                    int local_x = ((int)x - ui.actor_x) / 6;
                    int local_y = ((int)y - ui.actor_y) / 6;
                    if ((int)x < ui.actor_x || (int)y < ui.actor_y || local_x >= frame->width ||
                        local_y >= frame->height ||
                        !(frame->mask[(unsigned)local_y * frame->mask_stride +
                                      (unsigned)local_x / 8u] &
                          (1u << (7u - (unsigned)local_x % 8u))))
                        continue;
                    CHECK(y < 256u); /* No sprite extends below the ground line. */
                    bottom = y;
                    ++count;
                    center_sum += 2u * x + 1u;
                }
            }
            CHECK(count > 0u && bottom == 255u);
            /* Contact centroid stays within one physical pixel of x=233. */
            CHECK(center_sum >= count * 464u && center_sum <= count * 468u);
            const JelliAsset *asset = jelli_asset_find(1001u + form * 6u + pose);
            CHECK(asset && asset->ground_y_q8 && asset->ground_x_q8);
        }
    }
}

int main(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    test_render(&game, &ui);
    test_page_items();
    test_pet_feedback();
    test_navigation_and_actions();
    test_ring_moments_and_meter();
    test_slide_damage();
    test_particles();
    test_manual_stat_timing();
    test_grounded_poses();
    puts("Pet UI, page actions, and renderer damage behavior verified.");
    return 0;
}

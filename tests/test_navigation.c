#include "jelli/activities.h"
#include "jelli/pet_ui.h"
#include "jelli/sound.h"
#include "../core/pet_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
static void navigation(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliPet original = game.pets[0];
    jelli_pet_render(&surface, &game, &ui, 0, false);
    jelli_pet_ui_swipe(&ui, &game, -80, 0);
    jelli_pet_render(&surface, &game, &ui, 20, false);
    CHECK(ui.last_view.stat_index == 1 && ui.last_view.tile_phase == 0);
    jelli_pet_render(&surface, &game, &ui, 90000, false);
    CHECK(ui.last_view.stat_index == 1);
    jelli_pet_ui_swipe(&ui, &game, 80, 0);
    jelli_pet_ui_swipe(&ui, &game, 80, 0);
    CHECK(ui.stat_offset == JELLI_PET_STAT_COUNT - 1u);
    jelli_pet_ui_swipe(&ui, &game, -80, 0);
    CHECK(ui.stat_offset == 0);
    jelli_pet_ui_swipe(&ui, &game, 0, -80);
    CHECK(ui.menu_open && ui.page == JELLI_UI_HOME);
    jelli_pet_render(&surface, &game, &ui, 90000, false);
    CHECK(ui.last_view.ring_visible == 0);
    jelli_pet_render(&surface, &game, &ui, 90108, false);
    CHECK(ui.last_view.ring_visible >= 180 && ui.last_view.ring_visible < 230);
    jelli_pet_render(&surface, &game, &ui, 90360, false);
    CHECK(ui.last_view.ring_visible == 255 && !ui.last_view.ring_moving);
    ui.page = JELLI_UI_SETTINGS;
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(ui.menu_open && ui.page == JELLI_UI_CARE);
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(ui.menu_open && ui.page == JELLI_UI_HOME);
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(!ui.menu_open);
    CHECK(memcmp(original.needs, game.pets[0].needs, sizeof(original.needs)) == 0);
    CHECK(original.touch_load == game.pets[0].touch_load &&
          original.activity == game.pets[0].activity);
    CHECK(original.bond == game.pets[0].bond && original.ticks == game.pets[0].ticks);
}
static void clock_controls(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.menu_open = true;
    ui.page = JELLI_UI_SETTINGS;
    ui.clock_known = true;
    ui.clock_minute = 1439;
    jelli_pet_ui_tap(&ui, &game, 233, 267);
    CHECK(ui.clock_edit);
    jelli_pet_ui_tap(&ui, &game, 356, 355);
    CHECK(jelli_pet_clock_minute(&ui, &game.pets[0]) == 0);
    jelli_pet_ui_tap(&ui, &game, 110, 355);
    CHECK(jelli_pet_clock_minute(&ui, &game.pets[0]) == 1439);
    jelli_pet_ui_tap(&ui, &game, 405, 233);
    CHECK(jelli_pet_clock_minute(&ui, &game.pets[0]) == 59);
    jelli_pet_ui_tap(&ui, &game, 356, 111);
    CHECK(ui.timezone_minutes == 30 && jelli_pet_clock_minute(&ui, &game.pets[0]) == 89);
    for (unsigned i = 0; i < 80; ++i)
        jelli_pet_clock_action(&ui, 1);
    CHECK(ui.timezone_minutes == -720);
    for (unsigned i = 0; i < 80; ++i)
        jelli_pet_clock_action(&ui, 2);
    CHECK(ui.timezone_minutes == 840);
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(!ui.clock_edit && ui.page == JELLI_UI_SETTINGS && ui.menu_open);
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(ui.page == JELLI_UI_CARE && ui.menu_open);
}
static void unavailable_actions(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.food = 0u;
    ui.menu_open = true;
    CHECK(jelli_pet_ui_available(&ui, &game, 1u) == JELLI_OK);
    jelli_pet_ui_tap(&ui, &game, 110, 111);
    CHECK(ui.result == JELLI_OK && game.pets[0].activity == JELLI_EATING);
    CHECK(!ui.menu_open && ui.page == JELLI_UI_HOME && game.food == 0u);
    for (unsigned i = 0u; i < 10u; ++i)
        jelli_game_advance(&game, 500u);
    CHECK(game.food == 0u && game.pets[0].activity == JELLI_IDLE);
    ui.menu_open = true;
    game.pets[0].needs[JELLI_SATIETY] = 1000u;
    jelli_pet_ui_tap(&ui, &game, 110, 111);
    CHECK(ui.result == JELLI_FULL && strcmp(jelli_pet_menu_hint(&ui, &game), "TUMMY FULL") == 0);
    jelli_pet_ui_tap(&ui, &game, 356, 111); /* Water. */
    CHECK(ui.result == JELLI_OK && !ui.menu_open);
}

static void main_menu_choices(void)
{
    static const char *const labels[] = {"FOOD", "WATER", "PLAY", "CARE", "PETS", "GIFTS"};
    for (unsigned slot = 1u; slot <= 6u; ++slot) {
        JelliPetUiButton button;
        CHECK(jelli_pet_ui_button(JELLI_UI_HOME, slot, false, true, &button));
        CHECK(strcmp(button.label, labels[slot - 1u]) == 0);
        CHECK(button.bounds.width == 96u && button.bounds.height == 96u);
    }
    unsigned seen = 0u;
    JelliPetUi ui;
    jelli_pet_ui_init(&ui);
    for (unsigned n = 0u; n < jelli_pet_activity_count(); ++n) {
        ui.activity_page = (uint8_t)(n / 5u);
        unsigned choice = jelli_pet_activity_choice(&ui, n % 5u + 1u);
        if (choice < jelli_moment_count) {
            CHECK(jelli_moments[choice].kind != JELLI_MOMENT_FEED);
            ++seen;
        } else {
            CHECK(choice == jelli_moment_count && n + 1u == jelli_pet_activity_count());
        }
    }
    CHECK(seen + 1u == jelli_pet_activity_count());
    ui.menu_open = true;
    ui.page = JELLI_UI_SETTINGS;
    jelli_pet_ui_back(&ui);
    CHECK(ui.page == JELLI_UI_CARE);
    ui.page = JELLI_UI_PETS;
    jelli_pet_ui_back(&ui);
    CHECK(ui.page == JELLI_UI_HOME);
}

static void reversal_and_pause(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    jelli_pet_render(&surface, &game, &ui, 0, false);
    jelli_pet_ui_swipe(&ui, &game, 0, -80);
    jelli_pet_render(&surface, &game, &ui, 0, false);
    jelli_pet_render(&surface, &game, &ui, 108, false);
    unsigned visible = ui.last_view.ring_visible;
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    jelli_pet_render(&surface, &game, &ui, 108, false);
    CHECK(ui.last_view.ring_visible == visible);
    jelli_pet_render(&surface, &game, &ui, 170, false);
    visible = ui.last_view.ring_visible;
    jelli_pet_ui_swipe(&ui, &game, 0, -80);
    jelli_pet_render(&surface, &game, &ui, 170, false);
    CHECK(ui.last_view.ring_visible == visible);
    jelli_pet_render(&surface, &game, &ui, 200, false);
    CHECK(ui.last_view.ring_visible >= visible);
    ui.page = JELLI_UI_SETTINGS;
    jelli_pet_render(&surface, &game, &ui, 200, true);
    CHECK(!ui.last_view.ring_moving && ui.last_view.ring_visible == 255);
    jelli_pet_ui_tap(&ui, &game, 233, 229); /* Clock text is not the gear hit target. */
    CHECK(!ui.clock_edit);
    jelli_pet_ui_tap(&ui, &game, 233, 267);
    CHECK(ui.clock_edit);
    jelli_pet_render(&surface, &game, &ui, 200, true);
    CHECK(!ui.last_view.ring_moving && ui.last_view.ring_clock_edit);
}

static void volume_controls(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.menu_open = true;
    ui.page = JELLI_UI_SETTINGS;
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(ui.last_view.volume == 65u);
    jelli_pet_ui_tap(&ui, &game, 356, 355);
    CHECK(game.volume == 75u && ui.save_requested && ui.sound_pending);
    CHECK(ui.menu_open && ui.page == JELLI_UI_SETTINGS);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(ui.last_view.volume == 75u && surface.damage.width == JELLI_WIDTH);
    for (unsigned i = 0u; i < 12u; ++i)
        jelli_pet_ui_tap(&ui, &game, 110, 355);
    CHECK(game.volume == 0u);
    CHECK(jelli_pet_ui_available(&ui, &game, 5u) == JELLI_FULL);
    CHECK(jelli_pet_ui_available(&ui, &game, 6u) == JELLI_OK);
    jelli_pet_ui_tap(&ui, &game, 356, 355);
    CHECK(game.volume == 10u);
    ui.clock_edit = true;
    jelli_pet_ui_tap(&ui, &game, 356, 355);
    CHECK(game.volume == 10u && ui.clock_adjust == 1);
    ui.clock_edit = false;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_PLAY, 1u, 0u}) == JELLI_OK);
    jelli_pet_ui_tap(&ui, &game, 356, 355);
    CHECK(game.volume == 20u && ui.page == JELLI_UI_SETTINGS && ui.menu_open);
}

static void feedback(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    jelli_pet_ui_tap(&ui, &game, 233, 420);
    CHECK(ui.menu_open && jelli_particles_count(&ui.particles) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 0u) == JELLI_SOUND_CONFIRM + 1u);
    jelli_pet_ui_tap(&ui, &game, 356, 355);
    CHECK(ui.page == JELLI_UI_CARE && jelli_particles_count(&ui.particles) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 200u) == JELLI_SOUND_CONFIRM + 1u);
    jelli_pet_ui_tap(&ui, &game, 233, 440);
    CHECK(ui.page == JELLI_UI_HOME && jelli_particles_count(&ui.particles) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 400u) == JELLI_SOUND_BACK + 1u);
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(!ui.menu_open && jelli_particles_count(&ui.particles) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 600u) == JELLI_SOUND_BACK + 1u);
    ui.menu_open = true;
    ui.page = JELLI_UI_HOME;
    game.pets[0].hydration = 200u;
    jelli_pet_ui_tap(&ui, &game, 356, 111); /* Water. */
    CHECK(game.pets[0].hydration > 200u && jelli_particles_count(&ui.particles) > 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 800u) == JELLI_SOUND_PET + 1u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 801u) == 0u);
}

static void recovery_progress(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.pets[0].health = JELLI_UNWELL;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_CARE, 1u, 0u}) == JELLI_OK);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(ui.last_view.care_seconds == 30u);
    jelli_game_advance(&game, 800u);
    jelli_game_advance(&game, 200u);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(ui.last_view.care_seconds == 29u && surface.damage.width == JELLI_WIDTH);
    for (unsigned i = 0; i < 38u; ++i)
        jelli_game_advance(&game, 800u);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(ui.last_view.care_seconds == 0u && game.pets[0].health == JELLI_WELL);
}

static void menu_guidance(void)
{
    CHECK(jelli_pet_ui_starts(JELLI_UI_MOMENTS, 1u));
    CHECK(!jelli_pet_ui_starts(JELLI_UI_MOMENTS, 6u));
    CHECK(!jelli_pet_ui_starts(JELLI_UI_MOMENTS, 0u));
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.menu_open = true;
    ui.page = JELLI_UI_MOMENTS;
    ui.activity_page = (uint8_t)((jelli_pet_activity_count() - 1u) / 5u);
    game.pets[0].needs[JELLI_SATIETY] = 0;
    game.pets[0].hydration = 0;
    game.pets[0].needs[JELLI_ENERGY] = 0;
    unsigned exercise_slot = (jelli_pet_activity_count() - 1u) % 5u + 1u;
    JelliPetUiButton exercise;
    CHECK(jelli_pet_ui_control(&ui, exercise_slot, false, &exercise));
    jelli_pet_ui_tap(&ui, &game, (int)(exercise.bounds.x + 48u), (int)(exercise.bounds.y + 48u));
    CHECK(ui.result == JELLI_NOT_READY && ui.attempted_slot == exercise_slot);
    CHECK(strcmp(jelli_pet_menu_hint(&ui, &game), "FOOD FIRST") == 0);
    game.pets[0].needs[JELLI_SATIETY] = 500;
    CHECK(strcmp(jelli_pet_menu_hint(&ui, &game), "WATER FIRST") == 0);
    game.pets[0].hydration = 500;
    CHECK(strcmp(jelli_pet_menu_hint(&ui, &game), "REST FOR ENERGY") == 0);
    ui.page = JELLI_UI_HEALTH;
    game.pets[0].medicine_until = 36000u;
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(&ui, 2u, false, &button));
    jelli_pet_ui_tap(&ui, &game, 356, 111);
    CHECK(ui.result == JELLI_NOT_READY && ui.page == JELLI_UI_HEALTH);
    CHECK(strcmp(jelli_pet_menu_hint(&ui, &game), "DOSE GIVEN - WAIT") == 0);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(ui.last_view.unavailable & (1u << 2u));
    ui.page = JELLI_UI_HOME;
    game.pets[0].hydration = 1000;
    ui.last_view.ring_moving = false;
    jelli_pet_ui_tap(&ui, &game, 356, 111);
    CHECK(ui.result == JELLI_FULL);
    CHECK(strcmp(jelli_pet_menu_hint(&ui, &game), "HYDRATION FULL") == 0);
}

static void bottom_button_bounds(void)
{
    JelliGame game;
    JelliPetUi ui;
    JelliPetUiButton button;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    CHECK(jelli_pet_ui_control(&ui, 0u, false, &button));
    CHECK(button.bounds.x == 143u && button.bounds.y == 368u && button.bounds.width == 180u &&
          button.bounds.height == 80u);
    jelli_pet_ui_tap(&ui, &game, 142, 410);
    CHECK(!ui.menu_open);
    jelli_pet_ui_tap(&ui, &game, 143, 410);
    CHECK(ui.menu_open);
    CHECK(jelli_pet_ui_control(&ui, 0u, false, &button));
    CHECK(button.bounds.x == 173u && button.bounds.y == 390u && button.bounds.width == 120u &&
          button.bounds.height == 65u);
    jelli_pet_ui_tap(&ui, &game, 292, 410);
    CHECK(!ui.menu_open);
}

int main(void)
{
    bottom_button_bounds();
    navigation();
    main_menu_choices();
    menu_guidance();
    feedback();
    recovery_progress();
    volume_controls();
    clock_controls();
    unavailable_actions();
    reversal_and_pause();
    puts("Manual stat pages, menu easing, clock wrap and Back hierarchy verified.");
    return 0;
}

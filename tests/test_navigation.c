#include "jelli/pet_ui.h"
#include "jelli/sound.h"
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
    CHECK(ui.page == JELLI_UI_HOME && ui.menu_open);
}
static void unavailable_actions(void)
{
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events = {0};
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.events = &events;
    ui.menu_open = true;
    ui.page = JELLI_UI_CARE;
    jelli_pet_ui_tap(&ui, &game, 110, 111);
    CHECK(ui.page == JELLI_UI_FOOD && game.pets[0].activity == JELLI_IDLE);
    ui.sound_pending = false;
    events = (JelliEventLog){0};
    game.food = 0;
    CHECK(jelli_pet_ui_available(&ui, &game, 1) == JELLI_NO_ITEM);
    CHECK(events.sequence == 0 && game.pets[0].activity == JELLI_IDLE);
    jelli_pet_render(&surface, &game, &ui, 0, false);
    CHECK(ui.last_view.unavailable & 2u);
    jelli_pet_ui_tap(&ui, &game, 110, 111);
    CHECK(ui.result == JELLI_NO_ITEM && !ui.sound_pending);
    CHECK(game.pets[0].activity == JELLI_IDLE && !ui.save_requested);
    game.food = 1;
    jelli_pet_render(&surface, &game, &ui, 0, false);
    CHECK(!(ui.last_view.unavailable & 2u));
    jelli_pet_ui_tap(&ui, &game, 110, 111);
    CHECK(ui.result == JELLI_OK && game.pets[0].activity == JELLI_EATING);
    CHECK(!ui.menu_open && ui.page == JELLI_UI_HOME);
    ui.menu_open = true;
    ui.page = JELLI_UI_MOMENTS;
    CHECK(jelli_pet_ui_available(&ui, &game, 2) == JELLI_BUSY);
    CHECK(jelli_pet_ui_available(&ui, &game, 0) == JELLI_OK);
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

static void refill_food(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.food = 0u;
    ui.menu_open = true;
    ui.page = JELLI_UI_FOOD;
    CHECK(jelli_pet_ui_available(&ui, &game, 1u) == JELLI_NO_ITEM);
    CHECK(jelli_pet_ui_available(&ui, &game, 9u) == JELLI_OK);
    CHECK(game.food == 0u);
    jelli_pet_ui_tap(&ui, &game, 329, 322);
    CHECK(game.food == 5u && ui.save_requested);
    CHECK(ui.page == JELLI_UI_FOOD && ui.menu_open);
    CHECK(jelli_pet_ui_available(&ui, &game, 9u) == JELLI_FULL);
    jelli_pet_ui_tap(&ui, &game, 329, 322);
    CHECK(game.food == 5u);
    CHECK(jelli_pet_ui_available(&ui, &game, 1u) == JELLI_OK);
    jelli_pet_ui_tap(&ui, &game, 137, 130);
    CHECK(game.pets[0].activity == JELLI_EATING && !ui.menu_open);
    for (unsigned i = 0u; i < 10u; ++i)
        jelli_game_advance(&game, 500u);
    CHECK(game.food == 4u);
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
    jelli_pet_ui_tap(&ui, &game, 110, 111);
    CHECK(ui.page == JELLI_UI_CARE && jelli_particles_count(&ui.particles) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 200u) == JELLI_SOUND_CONFIRM + 1u);
    jelli_pet_ui_tap(&ui, &game, 233, 440);
    CHECK(ui.page == JELLI_UI_HOME && jelli_particles_count(&ui.particles) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 400u) == JELLI_SOUND_BACK + 1u);
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(!ui.menu_open && jelli_particles_count(&ui.particles) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 600u) == JELLI_SOUND_BACK + 1u);
    ui.menu_open = true;
    ui.page = JELLI_UI_CARE;
    game.pets[0].hydration = 200u;
    jelli_pet_ui_tap(&ui, &game, 61, 233); /* Water. */
    CHECK(game.pets[0].hydration > 200u && jelli_particles_count(&ui.particles) > 0u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 800u) == JELLI_SOUND_PET + 1u);
    CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 801u) == 0u);
}

int main(void)
{
    navigation();
    feedback();
    refill_food();
    volume_controls();
    clock_controls();
    unavailable_actions();
    reversal_and_pause();
    puts("Manual stat pages, menu easing, clock wrap and Back hierarchy verified.");
    return 0;
}

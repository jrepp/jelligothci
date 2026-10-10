#include "jelli/activities.h"
#include "jelli/assets.h"
#include "jelli/pet_ui.h"
#include "jelli/potty.h"

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

static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];

static void render(const JelliGame *game, JelliPetUi *ui, uint64_t ms)
{
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    jelli_pet_render(&surface, game, ui, ms, false);
}

static void tap(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    JelliPetUiButton b;
    CHECK(jelli_pet_ui_control(ui, slot, game->pets[game->active].asleep, &b));
    jelli_pet_ui_tap(ui, game, (int)(b.bounds.x + b.bounds.width / 2u),
                     (int)(b.bounds.y + b.bounds.height / 2u));
}

static void ring_items_come_from_data(void)
{
    JelliPetUiButton b;
    CHECK(jelli_pet_ui_button(JELLI_UI_MOMENTS, 6u, false, true, &b));
    CHECK(strcmp(b.label, jelli_moments[4].name) == 0 && b.icon == jelli_moments[4].icon);
    CHECK(jelli_asset_find(b.icon) != NULL);
    for (unsigned slot = 1u; slot <= 4u; ++slot) { /* Breakfast..movie keep their icons. */
        CHECK(jelli_pet_ui_button(JELLI_UI_MOMENTS, slot, false, true, &b));
        CHECK(b.icon == jelli_moments[slot - 1u].icon && b.icon == 6008u + slot);
    }
    CHECK(jelli_pet_ui_button(JELLI_UI_HEALTH, 6u, false, true, &b));
    CHECK(strcmp(b.label, "POTTY") == 0 && b.icon == jelli_pet_health_icon(JELLI_HEALTH_POTTY));
    CHECK(jelli_asset_find(b.icon) != NULL);
    for (unsigned a = 0u; a < JELLI_HEALTH_COUNT; ++a)
        CHECK(jelli_asset_find(jelli_pet_health_icon(a)) != NULL);
    CHECK(JELLI_UI_ACTION_COUNT <= JELLI_INPUT_FIRST);
}

static void potty_routine_from_the_ring(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.pets[0].potty = jelli_potty_rules.urge_threshold;
    render(&game, &ui, 0u);
    tap(&ui, &game, 0u); /* MENU */
    ui.page = JELLI_UI_HEALTH;
    render(&game, &ui, 1000u);
    render(&game, &ui, 3000u); /* Let the ring settle; moving rings ignore slot taps. */
    tap(&ui, &game, 6u);
    CHECK(ui.page == JELLI_UI_POTTY && jelli_pet_page_is_routine(ui.page));
    CHECK(jelli_pet_health_action(&ui) == JELLI_HEALTH_POTTY);
    CHECK(jelli_pet_routine_action(ui.page) == JELLI_UI_ACTION_POTTY);
    render(&game, &ui, 4000u);
    render(&game, &ui, 6000u);
    CHECK(ui.clicker_goal == 1u); /* One tap: the break happens once. */
    tap(&ui, &game, 1u);
    CHECK(ui.result == JELLI_OK && game.pets[0].potty == 0u);
    tap(&ui, &game, 1u);
    CHECK(ui.result == JELLI_FULL || ui.result == JELLI_NOT_READY);
    jelli_pet_ui_back(&ui);
    CHECK(ui.page == JELLI_UI_HEALTH);
}

static void reading_shows_its_book(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    render(&game, &ui, 0u);
    uint16_t before[JELLI_WIDTH * JELLI_HEIGHT];
    memcpy(before, pixels, sizeof(before));
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, game.pets[0].id, 4u}) ==
          JELLI_OK);
    render(&game, &ui, 0u);
    CHECK(ui.last_view.moment == 5u && ui.last_view.activity == JELLI_PLAYING);
    const JelliAsset *book = jelli_asset_find(jelli_moments[4].prop);
    CHECK(book != NULL);
    unsigned changed = 0u;
    for (unsigned i = 0u; i < JELLI_WIDTH * JELLI_HEIGHT; ++i)
        changed += pixels[i] != before[i];
    CHECK(changed > 0u);
}

int main(void)
{
    ring_items_come_from_data();
    potty_routine_from_the_ring();
    reading_shows_its_book();
    puts("PASS: reading and potty appear from data, run, and draw their props");
    return 0;
}

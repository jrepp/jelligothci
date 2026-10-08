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
    CHECK(surface.damage.width == JELLI_WIDTH && surface.damage.height == JELLI_HEIGHT);
    jelli_pet_render(&surface, game, ui, 450u, false);
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
    CHECK(region_changed(282u, 118u, 48u, 48u));

    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    memcpy(comparison, pixels, sizeof(comparison));
    JelliCommand care = {JELLI_CMD_CARE, game.pets[game.active].id, 0u};
    CHECK(jelli_game_command(&game, care) == JELLI_OK);
    game.pets[game.active].health = JELLI_RECOVERING;
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(region_changed(0u, 178u, JELLI_WIDTH, 12u));
}

static void test_page_items(void)
{
    for (unsigned page = 0u; page < 5u; ++page) {
        for (unsigned item = 0u; item < 6u; ++item) {
            JelliPetUiItem button = jelli_pet_ui_item((JelliPetPage)page, item, false);
            CHECK(button.label[0] != '\0');
            CHECK(jelli_pet_ui_item((JelliPetPage)page, item, true).label[0] != '\0');
        }
    }
    CHECK(jelli_pet_ui_item(JELLI_UI_CARE, 2u, true).action == JELLI_UI_ACTION_CLEAN_WAKE);
    CHECK(strcmp(jelli_pet_ui_item(JELLI_UI_CARE, 2u, true).label, "WAKE") == 0);
    CHECK(jelli_pet_ui_item(JELLI_UI_COLLECTION, 1u, false).action == JELLI_UI_ACTION_SWITCH_PET);
    CHECK(jelli_pet_ui_item(JELLI_UI_SETTINGS, 0u, false).action == JELLI_UI_ACTION_BEDTIME);
}

static void tap_item(JelliPetUi *ui, JelliGame *game, unsigned item)
{
    int x = 58 + (int)(item % 3u) * 116 + 20;
    int y = 310 + (int)(item / 3u) * 49 + 15;
    jelli_pet_ui_tap(ui, game, x, y);
}

static void test_navigation_and_actions(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    tap_item(&ui, &game, 1u);
    CHECK(ui.page == JELLI_UI_CARE);
    tap_item(&ui, &game, 2u);
    CHECK(game.pets[game.active].activity == JELLI_CLEANING);
    for (unsigned step = 0u; step < 10u; ++step)
        jelli_game_advance(&game, 1000u);
    CHECK(game.pets[game.active].activity == JELLI_IDLE);
    tap_item(&ui, &game, 5u);
    CHECK(ui.page == JELLI_UI_HOME);
    tap_item(&ui, &game, 4u);
    CHECK(ui.page == JELLI_UI_MORE);
    tap_item(&ui, &game, 0u);
    CHECK(game.pets[game.active].activity == JELLI_GIVING);
    CHECK(ui.save_requested);

    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    tap_item(&ui, &game, 3u);
    CHECK(ui.page == JELLI_UI_COLLECTION);
    tap_item(&ui, &game, 1u);
    CHECK(game.active == 1u);
    CHECK(ui.save_requested);

    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    tap_item(&ui, &game, 5u);
    CHECK(ui.page == JELLI_UI_SETTINGS);
    uint32_t bedtime = game.pets[game.active].bedtime;
    tap_item(&ui, &game, 0u);
    CHECK(game.pets[game.active].bedtime == (bedtime + 1u) % 24u);
    tap_item(&ui, &game, 1u);
    CHECK(ui.save_status == JELLI_SAVE_PENDING && ui.save_requested);

    game.resuming = true;
    game.resume_remaining_ms = 60000u;
    uint8_t page = (uint8_t)ui.page;
    tap_item(&ui, &game, 2u);
    CHECK((uint8_t)ui.page == page);
    CHECK(game.pets[game.active].bedtime == (bedtime + 1u) % 24u);
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
    puts("Pet UI, page actions, and renderer damage behavior verified.");
    return 0;
}

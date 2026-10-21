#include "game_fixture.h"
#include "jelli/pet_engine.h"
#include "jelli/collectible.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static JelliGame game;
static JelliPetUi ui;
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static void render(void)
{
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    jelli_pet_render(&surface, &game, &ui, 0u, false);
}
static void reset(void)
{
    test_game_pair(&game);
    jelli_pet_ui_init(&ui);
    ui.menu_open = true;
    ui.page = JELLI_UI_COLLECTION;
    render();
}
static void press(unsigned slot)
{
    JelliPetUiButton b;
    CHECK(jelli_pet_ui_control(&ui, slot, false, &b));
    CHECK(strstr(b.label, "GIVE") == NULL);
    jelli_pet_ui_tap(&ui, &game, (int)(b.bounds.x + b.bounds.width / 2u),
                     (int)(b.bounds.y + b.bounds.height / 2u));
    render();
}
static void browsing_missing_and_paging(void)
{
    reset();
    press(1u);
    CHECK(ui.page == JELLI_UI_GIFT_SHELF && ui.gift_category == 0u);
    press(6u);
    CHECK(ui.gift_page == 1u);
    press(2u);
    CHECK(ui.page == JELLI_UI_PRESENT_ACTION && ui.latched_prize == 6u);
    JelliPetUiButton missing;
    CHECK(!jelli_pet_ui_control(&ui, 1u, false, &missing));
    CHECK(!game.collectible.active);
    press(0u);
    CHECK(ui.page == JELLI_UI_GIFT_SHELF && ui.gift_page == 1u);
    jelli_pet_ui_swipe(&ui, &game, -1, 0);
    CHECK(ui.gift_page == 2u);
    render();
    press(6u);
    CHECK(ui.gift_page == 0u);
    press(0u);
    CHECK(ui.page == JELLI_UI_COLLECTION);
}
static void explicit_craft_retains_pending_offer(void)
{
    reset();
    const JelliCraftRecipe *r = &jelli_craft_recipes[0];
    game.prizes.owned = game.prizes.discovered = (uint16_t)((1u << r->first) | (1u << r->second));
    game.prizes.origin_pet[r->first] = game.prizes.origin_pet[r->second] = game.pets[0].id;
    jelli_prize_complete(&game, JELLI_PRIZE_WASH);
    unsigned offered = game.prizes.offered;
    CHECK(offered != 0u);
    render();
    press(2u);
    press(1u);
    press(1u);
    CHECK(ui.collectible_confirm == 1u);
    CHECK(!(game.prizes.owned & (1u << r->output)));
    press(0u);
    CHECK(!(game.prizes.owned & (1u << r->output)));
    press(1u);
    press(1u);
    press(1u);
    CHECK(game.prizes.owned & (1u << r->output));
    CHECK(game.prizes.owned & (1u << r->first));
    CHECK(game.prizes.owned & (1u << r->second));
    CHECK(game.prizes.offered == offered && !game.collectible.active);
    CHECK(ui.save_requested);
    press(1u);
    CHECK(game.collectible.active && game.collectible.prize == r->output);
}
static void catch_opens_permanent_play(void)
{
    reset();
    ui.menu_open = false;
    ui.page = JELLI_UI_HOME;
    jelli_prize_complete(&game, JELLI_PRIZE_WASH);
    render();
    jelli_pet_ui_tap(&ui, &game, 233, 110);
    render();
    CHECK(ui.page == JELLI_UI_PRESENT_ACTION && ui.latched_prize == 6u);
    CHECK(game.prizes.owned & (1u << 5u));
    press(1u);
    CHECK(game.collectible.active && game.prizes.owned & (1u << 5u));
    CHECK(ui.page == JELLI_UI_COLLECTIBLE_PLAY);
}
int main(void)
{
    browsing_missing_and_paging();
    explicit_craft_retains_pending_offer();
    catch_opens_permanent_play();
    puts("PASS: collection categories/paging, explicit craft, pending catch and permanent gifts");
    return 0;
}

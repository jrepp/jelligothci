#include "../core/pet_collectible_ui.h"
#include "jelli/collectible.h"
#include "jelli/save.h"
#include "jelli/sound.h"
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

static void win(void)
{
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.prizes.owned = game.prizes.discovered = UINT32_C(1) << JELLI_COLLECTIBLE_CHAIN_PRIZE;
    game.prizes.origin_pet[JELLI_COLLECTIBLE_CHAIN_PRIZE] = game.pets[0].id;
    CHECK(jelli_collectible_play(&game, JELLI_COLLECTIBLE_CHAIN_PRIZE) == JELLI_OK);
    game.collectible.chain = (JelliChain){.body = {6u, 5u, 4u, 3u, 2u, 1u, 0u},
                                          .length = 7u,
                                          .direction = JELLI_CHAIN_RIGHT,
                                          .pending_direction = JELLI_CHAIN_RIGHT,
                                          .pickup = 7u};
    for (unsigned i = 0u; i < JELLI_CHAIN_STEP_TICKS; ++i)
        jelli_collectible_tick(&game);
    CHECK(game.collectible.chain.status == JELLI_CHAIN_WON && jelli_game_valid(&game));
    CHECK(game.cartridge_stats[0].plays == 1u && game.cartridge_stats[0].wins == 1u);
    ui.page = JELLI_UI_COLLECTIBLE_PLAY;
    ui.menu_open = true;
}

static void finish_and_no_repeated_reward(void)
{
    win();
    JelliPet before = game.pets[0];
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 0u));
    CHECK(!ui.collectible_menu && ui.collectible_win_ms == 0u);
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 900u));
    CHECK(ui.collectible_win_ms == 900u);
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 2499u));
    CHECK(!jelli_pet_collectible_win_update(&ui, &game, 2500u));
    CHECK(ui.page == JELLI_UI_GIFT_SHELF && ui.gift_category == 1u && !ui.gift_page);
    CHECK(!game.collectible.active && game.collectible.paused && ui.save_requested);
    CHECK(!jelli_pet_collectible_win_update(&ui, &game, 9000u));
    const JelliPet *after = &game.pets[0];
    CHECK(memcmp(before.needs, after->needs, sizeof(before.needs)) == 0);
    CHECK(before.bond == after->bond && before.hydration == after->hydration);
    CHECK(memcmp(before.collectible_progress.play_ticks, after->collectible_progress.play_ticks,
                 sizeof(before.collectible_progress.play_ticks)) == 0);
    CHECK(before.collectible_progress.special_unlocked ==
          after->collectible_progress.special_unlocked);
    CHECK(before.collectible_progress.record_rewarded ==
          after->collectible_progress.record_rewarded);
}

static void paused_clock_and_reload(void)
{
    win();
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 1000u));
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 1500u));
    for (unsigned i = 0u; i < 20u; ++i)
        CHECK(jelli_pet_collectible_win_update(&ui, &game, 1500u));
    CHECK(ui.collectible_win_ms == 500u && game.collectible.active);
    JelliSave save = {.game = game}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    game = loaded.game;
    ui = (JelliPetUi){.page = JELLI_UI_COLLECTIBLE_PLAY};
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 50000u));
    CHECK(ui.collectible_win_ms == 0u);
    CHECK(!jelli_pet_collectible_win_update(&ui, &game, 52500u));
}

static void reduced_motion_is_static(void)
{
    win();
    ui.collectible_reduced_motion = true;
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    Canvas canvas = {
        .s = &surface, .left = 0, .top = 0, .right = JELLI_WIDTH, .bottom = JELLI_HEIGHT};
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 0u));
    jelli_pet_collectible_win_draw(&canvas, &ui, &game);
    uint32_t first = 0u;
    for (unsigned i = 0u; i < JELLI_WIDTH * JELLI_HEIGHT; ++i)
        first = first * 33u + pixels[i];
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 2000u));
    jelli_pet_collectible_win_draw(&canvas, &ui, &game);
    uint32_t second = 0u;
    for (unsigned i = 0u; i < JELLI_WIDTH * JELLI_HEIGHT; ++i)
        second = second * 33u + pixels[i];
    CHECK(first && first == second);
}

static void bubble_final_celebration(void)
{
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.prizes.owned = game.prizes.discovered = 1u << JELLI_COLLECTIBLE_BUBBLE_PRIZE;
    game.prizes.origin_pet[JELLI_COLLECTIBLE_BUBBLE_PRIZE] = game.pets[0].id;
    CHECK(jelli_collectible_play(&game, JELLI_COLLECTIBLE_BUBBLE_PRIZE) == JELLI_OK);
    memset(game.collectible.bubble.cells, 0, sizeof(game.collectible.bubble.cells));
    game.collectible.bubble.level = 2u;
    game.collectible.bubble.status = JELLI_BUBBLE_WON;
    game.collectible.paused = true;
    ui.page = JELLI_UI_COLLECTIBLE_PLAY;
    ui.menu_open = true;
    CHECK(jelli_game_valid(&game));
    JelliPet before = game.pets[0];
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 1u));
    CHECK(jelli_pet_collectible_win_update(&ui, &game, 900u));
    CHECK(!jelli_pet_collectible_win_update(&ui, &game, 2501u));
    CHECK(ui.page == JELLI_UI_GIFT_SHELF && ui.gift_category == 1u);
    CHECK(memcmp(before.needs, game.pets[0].needs, sizeof(before.needs)) == 0);
}

static void losses_return_without_replay_menu(void)
{
    const unsigned prizes[] = {JELLI_COLLECTIBLE_CHAIN_PRIZE, JELLI_COLLECTIBLE_BUBBLE_PRIZE};
    for (unsigned i = 0u; i < 2u; ++i) {
        jelli_game_init(&game);
        jelli_pet_ui_init(&ui);
        game.prizes.owned = game.prizes.discovered = 1u << prizes[i];
        game.prizes.origin_pet[prizes[i]] = game.pets[0].id;
        CHECK(jelli_collectible_play(&game, prizes[i]) == JELLI_OK);
        if (i)
            game.collectible.bubble.status = JELLI_BUBBLE_TOP;
        else
            game.collectible.chain.status = JELLI_CHAIN_LOST;
        game.collectible.paused = true;
        ui.page = JELLI_UI_COLLECTIBLE_PLAY;
        ui.menu_open = true;
        ui.sound_played = true;
        ui.last_sound_ms = 999u; /* A recent move cannot swallow the loss sound. */
        CHECK(jelli_game_valid(&game));
        CHECK(jelli_pet_collectible_win_update(&ui, &game, 1000u));
        CHECK(jelli_pet_ui_sound(&ui, &game.pets[0], 1000u) == JELLI_SOUND_SLEEPY + 1u);
        CHECK(jelli_pet_collectible_win_update(&ui, &game, 1000u));
        CHECK(!jelli_pet_ui_sound(&ui, &game.pets[0], 1000u));
        CHECK(!ui.collectible_menu);
        CHECK(jelli_pet_collectible_win_update(&ui, &game, 1999u));
        CHECK(!jelli_pet_collectible_win_update(&ui, &game, 2000u));
        CHECK(ui.page == JELLI_UI_GIFT_SHELF && !game.collectible.active);
        CHECK(game.cartridge_stats[i].plays == 1u && !game.cartridge_stats[i].wins);
    }
}

int main(void)
{
    losses_return_without_replay_menu();
    bubble_final_celebration();
    finish_and_no_repeated_reward();
    paused_clock_and_reload();
    reduced_motion_is_static();
    puts("PASS: Chain win floats, respects paused clock, returns to shelf and never re-awards");
    return 0;
}

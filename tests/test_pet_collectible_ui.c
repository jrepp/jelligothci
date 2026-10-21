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
static JelliPetEngine engine;
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static uint64_t now;
static uint64_t clock_ms(void *ctx)
{
    (void)ctx;
    return now;
}
static void present(void *ctx, const JelliSurface *surface)
{
    (void)ctx;
    CHECK(surface->pixels == pixels);
}
static void render(void)
{
    jelli_pet_render(&engine.surface, &engine.game, &engine.ui, now, false);
}
static void reset(void)
{
    now = 0u;
    CHECK(jelli_pet_init(&engine, (JelliPlatform){.now_ms = clock_ms, .present = present},
                         (JelliSurface){pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}}));
    test_game_pair(&engine.game);
    engine.game.prizes.owned = engine.game.prizes.discovered = JELLI_PRIZE_MASK;
    for (unsigned i = 0; i < jelli_prize_count; ++i)
        engine.game.prizes.origin_pet[i] = engine.game.pets[0].id;
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_COLLECTION;
    render();
}
static void press(unsigned slot)
{
    JelliPetUiButton b;
    CHECK(jelli_pet_ui_control(&engine.ui, slot, false, &b));
    CHECK(b.bounds.width >= 48u && b.bounds.height >= 46u);
    jelli_pet_ui_tap(&engine.ui, &engine.game, (int)(b.bounds.x + b.bounds.width / 2u),
                     (int)(b.bounds.y + b.bounds.height / 2u));
    render();
}
static void snapshot(const char *path)
{
    FILE *f;
#ifdef _MSC_VER
    CHECK(fopen_s(&f, path, "wb") == 0);
#else
    f = fopen(path, "wb");
#endif
    CHECK(f != NULL);
    CHECK(fprintf(f, "P6\n466 466\n255\n") > 0);
    for (unsigned i = 0; i < JELLI_WIDTH * JELLI_HEIGHT; ++i) {
        uint16_t p = pixels[i];
        const unsigned char rgb[3] = {(unsigned char)(((p >> 11) & 31u) * 255u / 31u),
                                      (unsigned char)(((p >> 5) & 63u) * 255u / 63u),
                                      (unsigned char)((p & 31u) * 255u / 31u)};
        CHECK(fwrite(rgb, 1u, 3u, f) == 3u);
    }
    CHECK(fclose(f) == 0);
}
static void playable_controls_and_pause(void)
{
    reset();
    snapshot("collectible-categories.ppm");
    press(2u);
    CHECK(engine.ui.page == JELLI_UI_GIFT_SHELF);
    CHECK(pixels[430u * JELLI_WIDTH + 233u] != pixels[400u * JELLI_WIDTH + 233u]);
    snapshot("collectible-cartridges.ppm");
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION);
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_COLLECTIBLE_PLAY);
    CHECK(engine.game.collectible.mode == JELLI_COLLECTIBLE_CHAIN);
    CHECK(engine.game.collectible.active && !engine.game.collectible.paused);
    snapshot("collectible-chain.ppm");
    engine.ui.collectible_win_ms = 1800u;
    render();
    CHECK(pixels[440u * JELLI_WIDTH + 233u] != pixels[400u * JELLI_WIDTH + 233u]);
    snapshot("collectible-win.ppm");
    engine.ui.collectible_win_ms = 0u;
    render();
    jelli_pet_ui_swipe(&engine.ui, &engine.game, 0, -80);
    CHECK(engine.game.collectible.chain.pending_direction == JELLI_CHAIN_UP);
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_PAUSE) == JELLI_OK);
    engine.ui.collectible_menu = true;
    render(); /* Focus-loss pause can resume without discarding the board. */
    CHECK(engine.game.collectible.paused && engine.ui.collectible_menu);
    uint8_t head = engine.game.collectible.chain.body[0];
    jelli_pet_ui_swipe(&engine.ui, &engine.game, 1, 0);
    CHECK(engine.game.collectible.chain.body[0] == head);
    snapshot("collectible-paused.ppm");
    press(1u);
    CHECK(!engine.game.collectible.paused && !engine.ui.collectible_menu);
    press(0u);
    CHECK(!engine.game.collectible.active && engine.ui.page == JELLI_UI_GIFT_SHELF);
    CHECK(engine.game.prizes.owned == JELLI_PRIZE_MASK);
    press(1u);
    press(1u);
    CHECK(!engine.game.collectible.paused);
    JelliPetUiButton hidden;
    for (unsigned slot = 1u; slot <= 6u; ++slot)
        CHECK(!jelli_pet_ui_control(&engine.ui, slot, false, &hidden));
}

static void confirm_and_cancel_replacement(void)
{
    reset();
    press(1u);
    press(1u);
    press(1u); /* Base present also uses permanent Play. */
    CHECK(engine.game.collectible.mode == JELLI_COLLECTIBLE_GENERIC);
    press(0u);
    press(0u);
    press(3u);
    snapshot("collectible-records.ppm");
    press(1u); /* Owned records launch directly from their shelf. */
    CHECK(engine.game.collectible.mode == JELLI_COLLECTIBLE_RECORD);
    CHECK(engine.game.prizes.owned == JELLI_PRIZE_MASK);
    CHECK(jelli_collectible_record_progress(&engine.game, JELLI_RECORD_SAMPLES / 2u) == JELLI_OK);
    render();
    CHECK(engine.ui.last_view.collectible.record_cursor == JELLI_RECORD_SAMPLES / 2u);
    JelliPetUiButton menu;
    CHECK(jelli_pet_ui_control(&engine.ui, 0u, false, &menu));
    CHECK(pixels[440u * JELLI_WIDTH + 233u] != pixels[400u * JELLI_WIDTH + 233u]);
    CHECK(strcmp(menu.label, "BACK") == 0);
    snapshot("collectible-record.ppm");
    press(0u);
    CHECK(engine.ui.page == JELLI_UI_GIFT_SHELF && !engine.game.collectible.active);
}
static void bubble_controls_and_checkpoint_replacement(void)
{
    reset();
    press(2u);
    press(2u);
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION);
    snapshot("bubble-launcher.ppm");
    press(1u); /* Large cartridge artwork launches with a real tap. */
    CHECK(engine.ui.page == JELLI_UI_COLLECTIBLE_PLAY);
    CHECK(engine.game.collectible.mode == JELLI_COLLECTIBLE_BUBBLE);
    snapshot("bubble-lab.ppm");
    press(6u);
    press(6u);
    CHECK(engine.game.collectible.bubble.assist == JELLI_BUBBLE_MANUAL);
    jelli_pet_ui_swipe(&engine.ui, &engine.game, -80, 0);
    jelli_pet_ui_tap(&engine.ui, &engine.game, 230, 200);
    CHECK(engine.game.collectible.bubble.input_count == 2u);
    jelli_collectible_tick(&engine.game);
    jelli_collectible_tick(&engine.game);
    CHECK(engine.game.collectible.bubble.x == 1u && engine.game.collectible.bubble.vertical == 1u);
    jelli_pet_ui_swipe(&engine.ui, &engine.game, 0, 80);
    jelli_collectible_tick(&engine.game);
    CHECK(engine.game.collectible.bubble.cursor == 2u);
    render();
    JelliPetUiButton hidden;
    for (unsigned slot = 2u; slot <= 5u; ++slot)
        CHECK(!jelli_pet_ui_control(&engine.ui, slot, false, &hidden));
    press(0u);
    JelliBubble checkpoint = engine.game.collectible.bubble;
    press(2u);
    press(2u); /* Primary RESUME button is an alternative to tapping the art. */
    CHECK(!engine.game.collectible.paused);
    CHECK(jelli_bubble_same(&checkpoint, &engine.game.collectible.bubble));
    press(0u);
    press(1u); /* Chain card explicitly labels replacement before launching. */
    CHECK(engine.ui.collectible_confirm == 2u);
    CHECK(engine.game.collectible.mode == JELLI_COLLECTIBLE_BUBBLE);
    press(0u); /* Cancel leaves the checkpoint intact. */
    CHECK(jelli_bubble_same(&checkpoint, &engine.game.collectible.bubble));
    press(1u);
    press(1u);
    CHECK(engine.game.collectible.mode == JELLI_COLLECTIBLE_CHAIN);
    reset();
    press(3u);
    press(1u);
    press(0u);
    press(0u);
    press(2u);
    press(2u);
    CHECK(engine.ui.collectible_confirm == 2u && engine.ui.latched_prize == 19u);
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION);
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_COLLECTIBLE_PLAY);
    CHECK(engine.game.collectible.mode == JELLI_COLLECTIBLE_BUBBLE);
}

static void background_music_setting(void)
{
    reset();
    engine.ui.page = JELLI_UI_SETTINGS;
    render();
    now += 1000u;
    render();
    CHECK(engine.game.activity_music_enabled);
    press(7u);
    CHECK(!engine.game.activity_music_enabled && engine.ui.save_requested);
    snapshot("activity-music-setting.ppm");
    press(7u);
    CHECK(engine.game.activity_music_enabled);
}

int main(void)
{
    background_music_setting();
    bubble_controls_and_checkpoint_replacement();
    playable_controls_and_pause();
    confirm_and_cancel_replacement();
    puts("PASS: permanent Play, tap controls, pause/ready, checkpoint replacement and native "
         "previews");
    return 0;
}

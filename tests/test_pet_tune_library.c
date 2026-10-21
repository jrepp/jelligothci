#include "game_fixture.h"
#include "jelli/pet_engine.h"
#include "jelli/collectible.h"
#include "jelli/save.h"
#include "../core/pet_tune_library.h"
#include "../core/pet_gallery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #e);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static JelliPetEngine engine;
static JelliGame before;
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static JelliMusicCommand command;
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
static bool audio_control(void *ctx, JelliMusicCommand value)
{
    (void)ctx;
    command = value;
    return true;
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
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_COLLECTION;
    render();
}
static void press(unsigned slot)
{
    JelliPetUiButton b;
    CHECK(jelli_pet_ui_control(&engine.ui, slot, false, &b));
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
    CHECK(f != NULL && fprintf(f, "P6\n466 466\n255\n") > 0);
    for (unsigned i = 0u; i < JELLI_WIDTH * JELLI_HEIGHT; ++i) {
        uint16_t p = pixels[i];
        const unsigned char rgb[] = {(unsigned char)(((p >> 11u) & 31u) * 255u / 31u),
                                     (unsigned char)(((p >> 5u) & 63u) * 255u / 63u),
                                     (unsigned char)((p & 31u) * 255u / 31u)};
        CHECK(fwrite(rgb, 1u, sizeof(rgb), f) == sizeof(rgb));
    }
    CHECK(!fclose(f));
}
static void encounter_without_audio(void)
{
    reset();
    CHECK(jelli_pet_library_count(engine.game.tunes_seen) == JELLI_COLLECTIBLE_RECORD_COUNT);
    engine.ui.menu_open = false;
    engine.game.activity_music_enabled = false;
    engine.game.volume = 0u;
    engine.game.pets[0].activity = JELLI_EXERCISING;
    engine.ui.save_requested = false;
    uint32_t revision = engine.game.revision;
    jelli_pet_music_sync(&engine);
    CHECK(engine.game.tunes_seen == (UINT32_C(1) << JELLI_MUSIC_WORKOUT));
    CHECK(engine.ui.save_requested && engine.game.revision == revision + 1u);
    CHECK(!engine.music_running);
    engine.ui.save_requested = false;
    jelli_pet_music_sync(&engine);
    CHECK(!engine.ui.save_requested && engine.game.revision == revision + 1u);
    engine.paused = true;
    engine.game.pets[0].activity = JELLI_EATING;
    jelli_pet_music_sync(&engine);
    CHECK(!(engine.game.tunes_seen & (UINT32_C(1) << JELLI_MUSIC_TEA)));
    engine.paused = false;
    jelli_pet_music_sync(&engine);
    CHECK(engine.game.tunes_seen & (UINT32_C(1) << JELLI_MUSIC_TEA));
    engine.game.pets[0].activity = JELLI_IDLE;
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_PRESENT_ACTION;
    engine.ui.latched_prize = JELLI_COLLECTIBLE_CHAIN_PRIZE + 1u;
    engine.game.prizes.owned |= UINT32_C(1) << JELLI_COLLECTIBLE_CHAIN_PRIZE;
    jelli_pet_music_sync(&engine);
    CHECK(engine.game.tunes_seen & (UINT32_C(1) << JELLI_MUSIC_LAUNCH));
    unsigned prize = JELLI_COLLECTIBLE_BUBBLE_PRIZE;
    engine.game.prizes.owned |= UINT32_C(1) << prize;
    engine.game.prizes.discovered = engine.game.prizes.owned;
    engine.game.prizes.origin_pet[prize] = engine.game.pets[0].id;
    engine.game.prizes.origin_pet[JELLI_COLLECTIBLE_CHAIN_PRIZE] = engine.game.pets[0].id;
    CHECK(jelli_collectible_play(&engine.game, prize) == JELLI_OK);
    engine.ui.page = JELLI_UI_COLLECTIBLE_PLAY;
    jelli_pet_music_sync(&engine);
    CHECK(engine.game.tunes_seen & (UINT32_C(1) << JELLI_MUSIC_BUBBLE));
    CHECK(!engine.music_running);
    CHECK(!engine.game.pets[0].collectible_progress.record_rewarded);
}
static void shelf_and_rare_records(void)
{
    reset();
    engine.game.tunes_seen = (uint16_t)JELLI_MUSIC_SEEN_MASK;
    render();
    press(3u);
    CHECK(pixels[90u * JELLI_WIDTH + 115u] == GOLD);
    snapshot("tune-library-rare.ppm");
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION && !engine.game.collectible.active);
    CHECK(!engine.game.prizes.owned);
    press(0u);
    press(6u);
    snapshot("tune-library-mixed.ppm");
    press(4u); /* First encountered tune follows the seven rare record cards. */
    CHECK(engine.ui.page == JELLI_UI_TUNE_PLAY && engine.ui.tune.song == JELLI_MUSIC_LAUNCH + 1u);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 0u);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 1500u);
    render();
    snapshot("tune-library-player.ppm");
    press(0u);
    CHECK(engine.ui.page == JELLI_UI_GIFT_SHELF && engine.ui.gift_page == 1u);
    press(6u);
    press(6u);
    CHECK(engine.ui.gift_page == 3u);
    JelliPetUiButton b;
    CHECK(!jelli_pet_ui_control(&engine.ui, 4u, false, &b));
    press(6u);
    CHECK(engine.ui.gift_page == 0u);
    unsigned prize = JELLI_COLLECTIBLE_RECORD_PRIZE;
    engine.game.prizes.owned = engine.game.prizes.discovered = UINT32_C(1) << prize;
    engine.game.prizes.origin_pet[prize] = engine.game.pets[0].id;
    render();
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_COLLECTIBLE_PLAY &&
          engine.game.collectible.mode == JELLI_COLLECTIBLE_RECORD);
    CHECK(jelli_collectible_record_progress(&engine.game, JELLI_RECORD_SAMPLES) == JELLI_OK);
    CHECK(engine.game.pets[0].collectible_progress.record_rewarded);
}
static void unchanged_gameplay(void)
{
    static JelliSave first, last;
    static uint8_t first_bytes[JELLI_SAVE_CAPACITY], last_bytes[JELLI_SAVE_CAPACITY];
    first.game = before;
    last.game = engine.game;
    size_t size = jelli_save_encode(&first, first_bytes, sizeof(first_bytes));
    CHECK(size && jelli_save_encode(&last, last_bytes, sizeof(last_bytes)) == size);
    CHECK(!memcmp(first_bytes, last_bytes, size));
    CHECK(before.revision == engine.game.revision && !engine.ui.save_requested);
}

static void replay_preserves_checkpoint_and_rewards(void)
{
    reset();
    unsigned prize = JELLI_COLLECTIBLE_BUBBLE_PRIZE;
    engine.game.prizes.owned = engine.game.prizes.discovered = UINT32_C(1) << prize;
    engine.game.prizes.origin_pet[prize] = engine.game.pets[0].id;
    CHECK(jelli_collectible_play(&engine.game, prize) == JELLI_OK);
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_CLOSE) == JELLI_OK);
    engine.game.tunes_seen = (uint16_t)(UINT32_C(1) << JELLI_MUSIC_WORKOUT);
    engine.game.activity_music_enabled = false;
    jelli_pet_ui_tap(&engine.ui, &engine.game, 233, 400); /* Sync the ordinary UI clock first. */
    before = engine.game;
    engine.ui.save_requested = false;
    jelli_pet_tune_start(&engine.ui, &engine.game, JELLI_MUSIC_WORKOUT);
    jelli_pet_bind_music(&engine, (JelliMusicDriver){.control = audio_control});
    jelli_pet_music_sync(&engine);
    CHECK(engine.music_running && command.song == JELLI_MUSIC_WORKOUT);
    CHECK(command.action == JELLI_MUSIC_START && command.cursor == 0u);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 0u);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 5000u);
    engine.paused = true;
    jelli_pet_music_sync(&engine);
    CHECK(engine.ui.tune.paused && !engine.music_running);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 20000u);
    CHECK(engine.ui.tune.elapsed_ms == 5000u);
    engine.paused = false;
    render();
    press(1u);
    jelli_pet_music_sync(&engine);
    CHECK(command.cursor == JELLI_MUSIC_RATE * 5u);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 20000u);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 44999u);
    CHECK(!engine.ui.tune.paused);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 45000u);
    CHECK(engine.ui.tune.paused && engine.ui.tune.elapsed_ms == 30000u);
    jelli_pet_music_progress(&engine);
    render();
    press(1u);
    jelli_pet_music_sync(&engine);
    CHECK(!command.cursor && command.action == JELLI_MUSIC_START);
    press(0u);
    unchanged_gameplay();
}
static void silent_play_and_interruption(void)
{
    reset();
    engine.game.tunes_seen = (uint16_t)(UINT32_C(1) << JELLI_MUSIC_WORKOUT);
    jelli_pet_tune_start(&engine.ui, &engine.game, JELLI_MUSIC_LAUNCH);
    CHECK(!engine.ui.tune.song);
    jelli_pet_tune_start(&engine.ui, &engine.game, JELLI_MUSIC_WORKOUT);
    engine.ui.collectible_reduced_motion = true;
    jelli_pet_music_sync(&engine); /* No audio driver: visual playback remains available. */
    jelli_pet_gallery_update(&engine.ui, &engine.game, 0u);
    jelli_pet_gallery_update(&engine.ui, &engine.game, 30000u);
    CHECK(engine.ui.tune.paused && engine.ui.tune.elapsed_ms == 30000u);
    engine.game.pets[0].asleep = true;
    jelli_pet_gallery_update(&engine.ui, &engine.game, 30001u);
    CHECK(!engine.ui.tune.song && engine.ui.page == JELLI_UI_GIFT_SHELF);
}
int main(void)
{
    encounter_without_audio();
    shelf_and_rare_records();
    replay_preserves_checkpoint_and_rewards();
    silent_play_and_interruption();
    puts("PASS: logical tune encounters, golden rare records, direct replay and reward isolation");
    return 0;
}

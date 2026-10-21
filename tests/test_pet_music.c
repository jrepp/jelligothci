#include "jelli/pet_engine.h"
#include "jelli/collectible.h"
#include "jelli/activities.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #e);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static JelliPetEngine engine;
typedef struct {
    JelliMusicStatus status;
    JelliMusicCommand last;
    bool reject;
} Audio;
static Audio audio;

static bool control(void *ctx, JelliMusicCommand command)
{
    Audio *a = ctx;
    if (a->reject)
        return false;
    a->last = command;
    a->status = (JelliMusicStatus){.cursor = command.cursor,
                                   .serial = command.serial,
                                   .playing = command.action == JELLI_MUSIC_START};
    return true;
}
/* Driver contract requires the same mutable context type for both callbacks. */
// cppcheck-suppress constParameterCallback
static bool status(void *ctx, JelliMusicStatus *out)
{
    const Audio *a = ctx;
    *out = a->status;
    return true;
}
static void start(void)
{
    engine = (JelliPetEngine){0};
    audio = (Audio){0};
    jelli_game_init(&engine.game);
    engine.game.prizes.owned |= (uint16_t)(1u << JELLI_COLLECTIBLE_RECORD_PRIZE);
    engine.game.prizes.discovered = engine.game.prizes.owned;
    engine.game.prizes.origin_pet[JELLI_COLLECTIBLE_RECORD_PRIZE] = engine.game.pets[0].id;
    CHECK(jelli_collectible_play(&engine.game, JELLI_COLLECTIBLE_RECORD_PRIZE) == JELLI_OK);
    jelli_pet_bind_music(&engine, (JelliMusicDriver){&audio, control, status});
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.action == JELLI_MUSIC_START);
}
static void every_record_selects_its_song(void)
{
    start();
    for (unsigned song = 0u; song < JELLI_COLLECTIBLE_RECORD_COUNT; ++song) {
        unsigned prize = JELLI_COLLECTIBLE_RECORD_PRIZE + song;
        engine.game.prizes.owned |= UINT32_C(1) << prize;
        engine.game.prizes.discovered |= UINT32_C(1) << prize;
        engine.game.prizes.origin_pet[prize] = engine.game.pets[0].id;
        CHECK(jelli_collectible_play(&engine.game, prize) == JELLI_OK);
        if (engine.game.collectible.paused)
            CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
        jelli_pet_music_sync(&engine);
        CHECK(audio.last.action == JELLI_MUSIC_START && audio.last.song == song);
    }
}

static void background_music(void)
{
    start();
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_CLOSE) == JELLI_OK);
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_PRESENT_ACTION;
    engine.ui.latched_prize = JELLI_COLLECTIBLE_CHAIN_PRIZE + 1u;
    engine.game.prizes.owned |= UINT32_C(1) << JELLI_COLLECTIBLE_CHAIN_PRIZE;
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.action == JELLI_MUSIC_START && audio.last.song == JELLI_MUSIC_LAUNCH);
    audio.status.cursor = JELLI_MUSIC_SAMPLES;
    audio.status.finished = true;
    uint32_t serial = engine.music_serial;
    jelli_pet_music_progress(&engine);
    CHECK(!engine.game.collectible.record_cursor);
    jelli_pet_music_sync(&engine);
    CHECK(engine.music_serial != serial && audio.last.cursor == 0u);
    engine.ui.page = JELLI_UI_COLLECTIBLE_PLAY;
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running && audio.last.action == JELLI_MUSIC_STOP);
    engine.ui.menu_open = false;
    JelliPet *pet = &engine.game.pets[engine.game.active];
    pet->activity = JELLI_CLEANING;
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.song == JELLI_MUSIC_WASH && engine.music_running);
    pet->activity = JELLI_EATING;
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.song == JELLI_MUSIC_TEA);
    engine.game.volume = 0u;
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.action == JELLI_MUSIC_VOLUME && audio.last.volume == 0u);
    engine.game.activity_music_enabled = false;
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running);
    engine.game.activity_music_enabled = true;
    pet->activity = JELLI_IDLE;
    pet->asleep = true;
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.song == JELLI_MUSIC_REST && engine.music_running);
    pet->asleep = false;
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running);
    for (unsigned i = 0u; i < jelli_moment_count; ++i) {
        pet->moment = (uint8_t)(i + 1u);
        pet->activity = JELLI_PLAYING;
        jelli_pet_music_sync(&engine);
        CHECK(engine.music_running && audio.last.song >= JELLI_MUSIC_GARDEN);
    }
    start();
    engine.game.activity_music_enabled = false;
    engine.game.volume = 20u;
    jelli_pet_music_sync(&engine);
    CHECK(engine.music_running && audio.last.song == 0u && audio.last.volume == 20u);
}
static void bubble_music(void)
{
    start();
    unsigned prize = JELLI_COLLECTIBLE_BUBBLE_PRIZE;
    engine.game.prizes.owned |= UINT32_C(1) << prize;
    engine.game.prizes.discovered |= UINT32_C(1) << prize;
    engine.game.prizes.origin_pet[prize] = engine.game.pets[0].id;
    CHECK(jelli_collectible_play(&engine.game, prize) == JELLI_OK);
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_COLLECTIBLE_PLAY;
    jelli_pet_music_sync(&engine);
    CHECK(engine.music_running && audio.last.song == JELLI_MUSIC_BUBBLE);
    audio.status.finished = true;
    uint32_t serial = engine.music_serial;
    jelli_pet_music_sync(&engine);
    CHECK(engine.music_serial != serial && audio.last.cursor == 0u);
    jelli_pet_music_progress(&engine);
    CHECK(engine.game.collectible.record_cursor == 0u);
    engine.game.activity_music_enabled = false;
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running);
    engine.game.activity_music_enabled = true;
    jelli_pet_music_sync(&engine);
    CHECK(engine.music_running);
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_PAUSE) == JELLI_OK);
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running);
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    jelli_pet_music_sync(&engine);
    CHECK(engine.music_running);
    engine.game.collectible.bubble.status = JELLI_BUBBLE_TOP;
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running);
    engine.game.collectible.bubble.status = JELLI_BUBBLE_WON;
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running);
    engine.game.collectible.bubble.status = JELLI_BUBBLE_RUNNING;
    engine.ui.page = JELLI_UI_GIFT_SHELF;
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running);
}

int main(void)
{
    bubble_music();
    background_music();
    every_record_selects_its_song();
    start();
    audio.status.cursor = 4410u;
    --audio.status.serial;
    jelli_pet_music_progress(&engine);
    CHECK(engine.game.collectible.record_cursor == 0u);
    audio.status.serial = engine.music_serial;
    jelli_pet_music_progress(&engine);
    CHECK(engine.game.collectible.record_cursor == 4410u);
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_PAUSE) == JELLI_OK);
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.action == JELLI_MUSIC_STOP);
    audio.status.cursor = 6000u; /* Queued tail after pause must not accrue credit. */
    jelli_pet_music_progress(&engine);
    CHECK(engine.game.collectible.record_cursor == 4410u);
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.action == JELLI_MUSIC_START && audio.last.cursor == 4410u);
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    audio.status.cursor = JELLI_MUSIC_SAMPLES;
    audio.status.finished = true;
    jelli_pet_music_progress(&engine);
    CHECK(engine.game.collectible.record_cursor == 0u); /* Old generation cannot finish new song. */
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.cursor == 0u && audio.last.action == JELLI_MUSIC_START);
    engine.game.volume = 0u;
    jelli_pet_music_sync(&engine);
    CHECK(audio.last.action == JELLI_MUSIC_VOLUME && audio.last.volume == 0u);
    audio.status.cursor = JELLI_MUSIC_SAMPLES;
    audio.status.finished = true;
    jelli_pet_music_progress(&engine);
    CHECK(engine.game.collectible.paused && engine.ui.save_requested);
    CHECK(engine.game.pets[engine.game.active].collectible_progress.record_rewarded);
    jelli_pet_music_sync(&engine);
    CHECK(!engine.music_running);
    start();
    audio.status.playing = false;
    jelli_pet_music_progress(&engine);
    CHECK(engine.game.collectible.paused && engine.ui.result == JELLI_NOT_READY);
    start();
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_PAUSE) == JELLI_OK);
    jelli_pet_music_sync(&engine);
    CHECK(jelli_collectible_control(&engine.game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    audio.reject = true;
    jelli_pet_music_sync(&engine);
    CHECK(engine.game.collectible.paused && engine.ui.result == JELLI_NOT_READY);
    puts("pet music bridge passed");
    return 0;
}

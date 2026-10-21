#include "jelli/pet_engine.h"
#include "jelli/collectible.h"
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
int main(void)
{
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

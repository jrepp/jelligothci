#include "jelli/pet_engine.h"
#include "jelli/collectible.h"
#include "jelli/activities.h"
#include <string.h>

void jelli_pet_bind_music(JelliPetEngine *engine, JelliMusicDriver driver)
{
    if (!engine)
        return;
    engine->music_driver = driver;
    engine->music_running = false;
    engine->music_serial = 0u;
    engine->music_cursor = 0u;
    engine->music_song = UINT8_MAX;
}

static bool record_selected(const JelliPetEngine *engine)
{
    const JelliCollectibleSession *session = &engine->game.collectible;
    return session->active && session->mode == JELLI_COLLECTIBLE_RECORD &&
           session->pet_id == engine->game.pets[engine->game.active].id;
}

void jelli_pet_music_progress(JelliPetEngine *engine)
{
    if (!engine->music_running || engine->music_song >= JELLI_MUSIC_LAUNCH ||
        !record_selected(engine) || engine->paused || engine->game.resuming ||
        engine->game.collectible.paused || !engine->music_driver.status ||
        engine->music_generation != engine->game.collectible.generation)
        return;
    JelliMusicStatus status = {0};
    if (!engine->music_driver.status(engine->music_driver.ctx, &status) ||
        status.serial != engine->music_serial || status.cursor > JELLI_MUSIC_SAMPLES ||
        status.cursor < engine->music_cursor)
        return;
    if (jelli_collectible_record_progress(&engine->game, status.cursor) == JELLI_OK) {
        engine->music_cursor = status.cursor;
        if (!status.playing && !status.finished) {
            (void)jelli_collectible_control(&engine->game, JELLI_COLLECTIBLE_PAUSE);
            engine->ui.result = JELLI_NOT_READY;
        }
        /* The session layer coalesces writes; never checkpoint each audio block. */
        if (status.finished) {
            engine->ui.save_requested = true;
            engine->ui.save_status = JELLI_SAVE_PENDING;
        }
    }
}

static bool send_command(JelliPetEngine *engine, JelliMusicAction action, uint32_t cursor,
                         uint8_t song)
{
    if (!engine->music_driver.control)
        return false;
    uint32_t serial = engine->music_serial + 1u; /* Serial wraps; only equality is used. */
    JelliMusicCommand command = {.action = action,
                                 .cursor = cursor,
                                 .serial = serial,
                                 .volume = engine->game.volume,
                                 .song = song};
    if (!engine->music_driver.control(engine->music_driver.ctx, command))
        return false;
    engine->music_serial = serial;
    engine->music_volume = engine->game.volume;
    engine->music_song = song;
    return true;
}

static void record_sync(JelliPetEngine *engine)
{
    const JelliCollectibleSession *session = &engine->game.collectible;
    uint8_t song = (uint8_t)(session->prize - JELLI_COLLECTIBLE_RECORD_PRIZE);
    bool wanted = record_selected(engine) && !session->paused && !engine->paused &&
                  !engine->game.resuming && !engine->game.pets[engine->game.active].asleep &&
                  session->record_cursor < JELLI_MUSIC_SAMPLES;
    if (!wanted) {
        if (engine->music_running && send_command(engine, JELLI_MUSIC_STOP, 0u, song))
            engine->music_running = false;
        return;
    }
    /* A restart or another pet is a new playback segment. Resume from the saved
     * cursor rather than crediting queued samples produced after a pause. */
    if (!engine->music_running || engine->music_song != song ||
        engine->music_pet != session->pet_id || engine->music_generation != session->generation ||
        session->record_cursor < engine->music_cursor) {
        if (send_command(engine, JELLI_MUSIC_START, session->record_cursor, song)) {
            engine->music_running = true;
            engine->music_pet = session->pet_id;
            engine->music_generation = session->generation;
            engine->music_cursor = session->record_cursor;
        } else {
            (void)jelli_collectible_control(&engine->game, JELLI_COLLECTIBLE_PAUSE);
            engine->ui.result = JELLI_NOT_READY;
        }
    } else if (engine->music_volume != engine->game.volume) {
        (void)send_command(engine, JELLI_MUSIC_VOLUME, session->record_cursor, song);
    }
}

static uint8_t moment_song(const JelliMoment *moment)
{
    if (moment->kind == JELLI_MOMENT_FEED)
        return JELLI_MUSIC_TEA;
    if (moment->animation == JELLI_ANIM_HOLD)
        return !strcmp(moment->name, "GOING OUT") ? JELLI_MUSIC_GARDEN : JELLI_MUSIC_REST;
    switch (moment->animation) {
    case JELLI_ANIM_CAST:
    case JELLI_ANIM_DIG:
    case JELLI_ANIM_CATCH:
        return JELLI_MUSIC_GARDEN;
    case JELLI_ANIM_SIP:
        return JELLI_MUSIC_TEA;
    case JELLI_ANIM_WATCH:
    case JELLI_ANIM_BREATHE:
    case JELLI_ANIM_DREAM:
    case JELLI_ANIM_REST:
    case JELLI_ANIM_THINK:
    case JELLI_ANIM_SKETCH:
        return JELLI_MUSIC_REST;
    case JELLI_ANIM_SWIM:
        return JELLI_MUSIC_WASH;
    default:
        return JELLI_MUSIC_PLAY;
    }
}

static uint8_t background_song(const JelliPetEngine *engine)
{
    const JelliPetUi *ui = &engine->ui;
    const JelliPet *pet = &engine->game.pets[engine->game.active];
    if (!engine->game.activity_music_enabled || engine->paused || engine->game.resuming)
        return UINT8_MAX;
    if (ui->menu_open) {
        unsigned prize = ui->latched_prize ? ui->latched_prize - 1u : JELLI_PRIZE_COUNT;
        return ui->page == JELLI_UI_PRESENT_ACTION &&
                       jelli_cartridge_index(prize) < JELLI_CARTRIDGE_COUNT &&
                       (engine->game.prizes.owned & (UINT32_C(1) << prize))
                   ? JELLI_MUSIC_LAUNCH
                   : UINT8_MAX;
    }
    if (pet->asleep)
        return JELLI_MUSIC_REST;
    if (pet->activity == JELLI_IDLE)
        return UINT8_MAX;
    if (pet->moment && pet->moment <= jelli_moment_count)
        return moment_song(&jelli_moments[pet->moment - 1u]);
    switch (pet->activity) {
    case JELLI_EATING:
    case JELLI_GIVING:
        return JELLI_MUSIC_TEA;
    case JELLI_CLEANING:
    case JELLI_CARING:
        return JELLI_MUSIC_WASH;
    default:
        return JELLI_MUSIC_PLAY;
    }
}

void jelli_pet_music_sync(JelliPetEngine *engine)
{
    if (record_selected(engine) && !engine->game.collectible.paused) {
        record_sync(engine);
        return;
    }
    uint8_t song = background_song(engine);
    if (song == UINT8_MAX) {
        if (engine->music_running && send_command(engine, JELLI_MUSIC_STOP, 0u, engine->music_song))
            engine->music_running = false;
        return;
    }
    JelliMusicStatus status = {0};
    bool ended = engine->music_running && engine->music_driver.status &&
                 engine->music_driver.status(engine->music_driver.ctx, &status) &&
                 status.serial == engine->music_serial && status.finished;
    if (!engine->music_running || engine->music_song != song || ended) {
        if (send_command(engine, JELLI_MUSIC_START, 0u, song)) {
            engine->music_running = true;
            engine->music_cursor = 0u;
        }
    } else if (engine->music_volume != engine->game.volume) {
        (void)send_command(engine, JELLI_MUSIC_VOLUME, 0u, song);
    }
}

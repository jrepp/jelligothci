#include "jelli/pet_engine.h"
#include "jelli/collectible.h"

void jelli_pet_bind_music(JelliPetEngine *engine, JelliMusicDriver driver)
{
    if (!engine)
        return;
    engine->music_driver = driver;
    engine->music_running = false;
    engine->music_serial = 0u;
    engine->music_cursor = 0u;
}

static bool record_selected(const JelliPetEngine *engine)
{
    const JelliCollectibleSession *session = &engine->game.collectible;
    return session->active && session->mode == JELLI_COLLECTIBLE_RECORD &&
           session->pet_id == engine->game.pets[engine->game.active].id;
}

void jelli_pet_music_progress(JelliPetEngine *engine)
{
    if (!engine->music_running || !record_selected(engine) || engine->paused ||
        engine->game.resuming || engine->game.collectible.paused || !engine->music_driver.status ||
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

static bool send_command(JelliPetEngine *engine, JelliMusicAction action, uint32_t cursor)
{
    if (!engine->music_driver.control)
        return false;
    uint32_t serial = engine->music_serial + 1u; /* Serial wraps; only equality is used. */
    JelliMusicCommand command = {
        .action = action,
        .cursor = cursor,
        .serial = serial,
        .volume = engine->game.volume,
        .song = record_selected(engine)
                    ? (uint8_t)(engine->game.collectible.prize - JELLI_COLLECTIBLE_RECORD_PRIZE)
                    : 0u};
    if (!engine->music_driver.control(engine->music_driver.ctx, command))
        return false;
    engine->music_serial = serial;
    engine->music_volume = engine->game.volume;
    return true;
}

void jelli_pet_music_sync(JelliPetEngine *engine)
{
    const JelliCollectibleSession *session = &engine->game.collectible;
    bool wanted = record_selected(engine) && !session->paused && !engine->paused &&
                  !engine->game.resuming && !engine->game.pets[engine->game.active].asleep &&
                  session->record_cursor < JELLI_MUSIC_SAMPLES;
    if (!wanted) {
        if (engine->music_running && send_command(engine, JELLI_MUSIC_STOP, 0u))
            engine->music_running = false;
        return;
    }
    /* A restart or another pet is a new playback segment. Resume from the saved
     * cursor rather than crediting queued samples produced after a pause. */
    if (!engine->music_running || engine->music_pet != session->pet_id ||
        engine->music_generation != session->generation ||
        session->record_cursor < engine->music_cursor) {
        if (send_command(engine, JELLI_MUSIC_START, session->record_cursor)) {
            engine->music_running = true;
            engine->music_pet = session->pet_id;
            engine->music_generation = session->generation;
            engine->music_cursor = session->record_cursor;
        } else {
            (void)jelli_collectible_control(&engine->game, JELLI_COLLECTIBLE_PAUSE);
            engine->ui.result = JELLI_NOT_READY;
        }
    } else if (engine->music_volume != engine->game.volume) {
        (void)send_command(engine, JELLI_MUSIC_VOLUME, session->record_cursor);
    }
}

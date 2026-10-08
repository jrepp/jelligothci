#include "session.h"
#include "storage.h"
#include <stdio.h>
#include <time.h>

_Static_assert(sizeof(JelliSession) <= 16384u, "Save workspace exceeds 16 KiB budget");

static uint64_t wall_time(const JelliOptions *options, uint64_t monotonic_ms, bool *valid)
{
    if (options->headless) {
        *valid = UINT64_MAX - options->wall_ms >= monotonic_ms;
        return *valid ? options->wall_ms + monotonic_ms : 0u;
    }
    struct timespec stamp;
    *valid = timespec_get(&stamp, TIME_UTC) == TIME_UTC && stamp.tv_sec >= (time_t)0 &&
             stamp.tv_nsec >= 0 && stamp.tv_nsec < 1000000000L;
    if (!*valid || (uint64_t)stamp.tv_sec > (UINT64_MAX - 999u) / 1000u) {
        *valid = false;
        return 0u;
    }
    return (uint64_t)stamp.tv_sec * 1000u + (uint64_t)stamp.tv_nsec / 1000000u;
}

static bool reconcile_sequence(JelliSession *session)
{
    JelliStorageResult result = jelli_sdl_storage_load(session->path, &session->snapshot,
                                                       session->bytes, sizeof(session->bytes));
    if (result == JELLI_STORAGE_OK) {
        session->sequence = session->snapshot.sequence;
        return true;
    }
    return result == JELLI_STORAGE_NO_SAVE;
}

bool jelli_sdl_session_save(JelliSession *session, JelliPetEngine *engine,
                            const JelliOptions *options)
{
    engine->ui.save_requested = false;
    if (!session->path) {
        engine->ui.save_status = JELLI_SAVE_UNAVAILABLE;
        return true;
    }
    if ((session->needs_reconcile && !reconcile_sequence(session)) ||
        session->sequence == UINT64_MAX || engine->game.resuming) {
        engine->ui.save_status = JELLI_SAVE_FAILED;
        return false;
    }
    /* Capture a paired host sample and drain only the bounded admitted backlog.
     * IO follows this cut; its completion must not replace the captured anchor. */
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    bool valid = false;
    uint64_t wall = wall_time(options, now, &valid);
    uint64_t elapsed = now >= engine->last_ms ? now - engine->last_ms : 0u;
    if (!engine->paused)
        jelli_game_advance(&engine->game, elapsed);
    for (unsigned n = 0; n < 3u && engine->game.backlog_ms >= 100u; ++n)
        jelli_game_advance(&engine->game, 0u);
    engine->last_ms = now;
    session->snapshot = (JelliSave){.game = engine->game,
                                    .sequence = session->sequence + 1u,
                                    .anchor_ms = wall,
                                    .anchor_valid = valid};
    JelliStorageResult result = jelli_sdl_storage_write(session->path, &session->snapshot,
                                                        session->bytes, sizeof(session->bytes));
    engine->ui.save_status = result == JELLI_STORAGE_OK ? JELLI_SAVE_SAVED : JELLI_SAVE_FAILED;
    if (result != JELLI_STORAGE_OK) {
        session->needs_reconcile = true;
        fprintf(stderr, "Save failed (%u); existing valid slot preserved. Session is unsaved.\n",
                (unsigned)result);
        return false;
    }
    session->needs_reconcile = false;
    session->sequence = session->snapshot.sequence;
    session->last_save_ms = now;
    return true;
}

static bool resume(JelliSession *session, JelliPetEngine *engine, const JelliOptions *options)
{
    bool valid = false;
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    uint64_t wall = wall_time(options, now, &valid);
    engine->game = session->snapshot.game;
    session->sequence = session->snapshot.sequence;
    bool trusted = valid && session->snapshot.anchor_valid && wall >= session->snapshot.anchor_ms;
    engine->ui.time_unavailable = !trusted;
    uint64_t elapsed = trusted ? wall - session->snapshot.anchor_ms : 0u;
    jelli_game_resume_begin(&engine->game, elapsed);
    /* Each call remains bounded. The boot loop renders the resume state without
     * admitting input; at most 46 calls cover the six-hour policy. */
    for (unsigned n = 0; n < 46u && engine->game.resuming; ++n) {
        (void)jelli_game_resume_step(&engine->game);
        jelli_pet_render(&engine->surface, &engine->game, &engine->ui, 0u, false);
        engine->platform.present(engine->platform.ctx, &engine->surface);
    }
    if (engine->game.resuming)
        return false;
    /* Explicitly forgive resume processing, then commit before live commands. */
    engine->last_ms = engine->platform.now_ms(engine->platform.ctx);
    return jelli_sdl_session_save(session, engine, options);
}

bool jelli_sdl_session_open(JelliSession *session, JelliPetEngine *engine,
                            const JelliOptions *options)
{
    *session = (JelliSession){.path = options->save_path};
    if (!session->path)
        return true;
    JelliStorageResult result = jelli_sdl_storage_load(session->path, &session->snapshot,
                                                       session->bytes, sizeof(session->bytes));
    if (result == JELLI_STORAGE_NO_SAVE)
        return jelli_sdl_session_save(session, engine, options);
    if (result == JELLI_STORAGE_OK)
        return resume(session, engine, options);
    fprintf(stderr,
            "Cannot load saves (%u). Files preserved; choose another --save path or omit\n"
            "--save for an explicitly unsaved session.\n",
            (unsigned)result);
    return false;
}

void jelli_sdl_session_update(JelliSession *session, JelliPetEngine *engine,
                              const JelliOptions *options)
{
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    bool periodic =
        session->path && now >= session->last_save_ms && now - session->last_save_ms >= 60000u;
    if (engine->ui.save_requested || periodic) {
        if (!jelli_sdl_session_save(session, engine, options))
            session->last_save_ms = now; /* Avoid retrying failed IO every frame. */
    }
}

#include "session.h"
#include "storage.h"
#include <stdio.h>
#include <time.h>

_Static_assert(sizeof(JelliSession) <= 16384u, "Save workspace exceeds 16 KiB budget");

static uint64_t wall_time(const JelliSession *session, const JelliOptions *options,
                          uint64_t monotonic_ms, bool *valid)
{
    if (session->clock_override) {
        uint64_t elapsed = monotonic_ms >= session->override_monotonic_ms
                               ? monotonic_ms - session->override_monotonic_ms
                               : 0u;
        *valid = monotonic_ms >= session->override_monotonic_ms &&
                 elapsed <= UINT64_MAX - session->override_wall_ms;
        return *valid ? session->override_wall_ms + elapsed : 0u;
    }
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
    uint64_t wall = wall_time(session, options, now, &valid);
    uint64_t elapsed = now >= engine->last_ms ? now - engine->last_ms : 0u;
    if (!engine->paused)
        jelli_game_advance(&engine->game, elapsed);
    for (unsigned n = 0; n < 3u && engine->game.backlog_ms >= 100u; ++n)
        jelli_game_advance(&engine->game, 0u);
    engine->last_ms = now;
    engine->game.timezone_minutes = engine->ui.timezone_minutes;
    engine->game.clock_adjust = engine->ui.clock_adjust;
    engine->game.wall_known = valid;
    engine->game.wall_seconds = wall / 1000u;
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
    session->saved_ticks = engine->game.ticks;
    return true;
}

static bool resume(JelliSession *session, JelliPetEngine *engine, const JelliOptions *options)
{
    bool valid = false;
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    uint64_t wall = wall_time(session, options, now, &valid);
    engine->game = session->snapshot.game;
    engine->ui.timezone_minutes = engine->game.timezone_minutes;
    engine->ui.clock_adjust = engine->game.clock_adjust;
    engine->game.wall_known = valid;
    engine->game.wall_seconds = wall / 1000u;
    session->sequence = session->snapshot.sequence;
    bool trusted = valid && session->snapshot.anchor_valid && wall >= session->snapshot.anchor_ms;
    engine->ui.time_unavailable = !trusted;
    uint64_t elapsed = trusted ? wall - session->snapshot.anchor_ms : 0u;
    jelli_game_resume_begin(&engine->game, elapsed);
    /* Each call remains bounded. The boot loop renders the resume state without
     * admitting input; a fixed number of one-minute calls covers the offline cap. */
    for (unsigned n = 0; n <= JELLI_OFFLINE_CAP_MS / 60000u && engine->game.resuming; ++n) {
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

static void initialize_zone(JelliPetEngine *engine, const JelliOptions *options)
{
    if (options->headless)
        return;
    time_t stamp = time(NULL);
#ifdef _MSC_VER
    struct tm local_value = {0};
    const struct tm *local =
        stamp != (time_t)-1 && localtime_s(&local_value, &stamp) == 0 ? &local_value : NULL;
#else
    const struct tm *local = stamp == (time_t)-1 ? NULL : localtime(&stamp);
#endif
    if (!local || stamp < (time_t)0)
        return;
    int local_minute = local->tm_hour * 60 + local->tm_min;
    int local_day = local->tm_yday, local_year = local->tm_year;
#ifdef _MSC_VER
    struct tm utc_value = {0};
    const struct tm *utc = gmtime_s(&utc_value, &stamp) == 0 ? &utc_value : NULL;
#else
    const struct tm *utc = gmtime(&stamp);
#endif
    if (!utc)
        return;
    int day_delta = local_year > utc->tm_year   ? 1
                    : local_year < utc->tm_year ? -1
                                                : local_day - utc->tm_yday;
    int offset = day_delta * 1440 + local_minute - utc->tm_hour * 60 - utc->tm_min;
    if (offset < -720 || offset > 840)
        return;
    engine->ui.timezone_minutes = (int16_t)offset;
    engine->game.timezone_minutes = (int16_t)offset;
}

bool jelli_sdl_session_open(JelliSession *session, JelliPetEngine *engine,
                            const JelliOptions *options)
{
    *session = (JelliSession){.path = options->save_path};
    if (!session->path) {
        initialize_zone(engine, options);
        return true;
    }
    JelliStorageResult result = jelli_sdl_storage_load(session->path, &session->snapshot,
                                                       session->bytes, sizeof(session->bytes));
    if (result == JELLI_STORAGE_NO_SAVE) {
        initialize_zone(engine, options);
        return jelli_sdl_session_save(session, engine, options);
    }
    if (result == JELLI_STORAGE_OK)
        return resume(session, engine, options);
    fprintf(stderr,
            "Cannot load saves (%u). Files preserved; choose another --save path or omit\n"
            "--save for an explicitly unsaved session.\n",
            (unsigned)result);
    return false;
}

static void update_clock(JelliSession *session, JelliPetEngine *engine, const JelliOptions *options,
                         uint64_t now)
{
    bool valid = false;
    uint64_t wall = wall_time(session, options, now, &valid);
    engine->game.wall_known = valid;
    engine->game.wall_seconds = wall / 1000u;
    if (session->clock_sampled && now >= session->last_clock_ms &&
        now - session->last_clock_ms < 1000u)
        return;
    session->clock_sampled = true;
    session->last_clock_ms = now;
    engine->ui.clock_known = valid;
    engine->ui.clock_minute = (uint16_t)(wall / 60000u % 1440u);
}

void jelli_sdl_session_clock(JelliSession *session, JelliPetEngine *engine,
                             const JelliOptions *options)
{
    update_clock(session, engine, options, engine->platform.now_ms(engine->platform.ctx));
}

void jelli_sdl_session_update(JelliSession *session, JelliPetEngine *engine,
                              const JelliOptions *options)
{
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    if (engine->ui.wall_set_requested) {
        engine->ui.wall_set_requested = false;
        bool valid = engine->ui.wall_set_seconds >= UINT64_C(946684800) &&
                     engine->ui.wall_set_seconds < UINT64_C(4102444800) &&
                     engine->ui.wall_set_offset_minutes >= -720 &&
                     engine->ui.wall_set_offset_minutes <= 840;
        if (valid) {
            session->clock_override = true;
            session->override_wall_ms = engine->ui.wall_set_seconds * 1000u;
            session->override_monotonic_ms = now;
            session->clock_sampled = false;
            engine->ui.timezone_minutes = engine->ui.wall_set_offset_minutes;
            engine->ui.clock_adjust = 0;
            engine->ui.save_requested = true;
        }
        engine->ui.result = valid ? JELLI_OK : JELLI_NOT_READY;
    }
    update_clock(session, engine, options, now);
    bool periodic = session->path && session->saved_ticks != engine->game.ticks &&
                    now >= session->last_save_ms && now - session->last_save_ms >= 60000u;
    if (engine->ui.save_requested || periodic) {
        if (!jelli_sdl_session_save(session, engine, options))
            session->last_save_ms = now; /* Avoid retrying failed IO every frame. */
    }
}

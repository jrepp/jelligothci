#include "jelli/sleep_log.h"

#include <limits.h>
#include <stddef.h>

static unsigned latest_index(const JelliSleepLog *log)
{
    return ((unsigned)log->head + JELLI_SLEEP_SESSION_CAPACITY - 1u) % JELLI_SLEEP_SESSION_CAPACITY;
}

static bool session_valid(const JelliSleepSession *session, bool active)
{
    if (session->flags > 15u || session->bed_energy > 1000u || session->bed_sleep_score > 1000u ||
        (!(session->flags & JELLI_SLEEP_STATS_KNOWN) &&
         (session->bed_energy != 0u || session->bed_sleep_score != 0u)) ||
        (!(session->flags & JELLI_SLEEP_BED_KNOWN) && session->bed_unix_seconds != 0u) ||
        (!(session->flags & JELLI_SLEEP_WAKE_KNOWN) && session->wake_unix_seconds != 0u))
        return false;
    if (active)
        return session->duration_seconds == 0u && session->wake_unix_seconds == 0u &&
               !(session->flags & (JELLI_SLEEP_WAKE_KNOWN | JELLI_SLEEP_REAL_DURATION));
    if (!(session->flags & JELLI_SLEEP_REAL_DURATION))
        return true;
    if ((session->flags & 3u) != 3u || session->wake_unix_seconds < session->bed_unix_seconds)
        return false;
    uint64_t duration = session->wake_unix_seconds - session->bed_unix_seconds;
    return session->duration_seconds == (duration > UINT32_MAX ? UINT32_MAX : duration);
}

bool jelli_sleep_log_valid(const JelliSleepLog *log)
{
    if (!log || log->head >= JELLI_SLEEP_SESSION_CAPACITY ||
        log->count > JELLI_SLEEP_SESSION_CAPACITY ||
        (log->count < JELLI_SLEEP_SESSION_CAPACITY && log->head != log->count))
        return false;
    if (!log->count)
        return !log->active && !log->pet_id && !log->total_seconds;
    if (!log->pet_id)
        return false;
    uint64_t durations = 0u;
    for (unsigned i = 0u; i < log->count; ++i) {
        bool active = log->active && i == latest_index(log);
        if (!session_valid(&log->sessions[i], active))
            return false;
        durations += log->sessions[i].duration_seconds;
    }
    return durations <= log->total_seconds;
}

bool jelli_sleep_log_begin(JelliSleepLog *log, uint32_t pet_id, uint64_t tick, bool known,
                           uint64_t seconds)
{
    if (!jelli_sleep_log_valid(log) || log->active || !pet_id)
        return false;
    log->sessions[log->head] = (JelliSleepSession){.bed_unix_seconds = known ? seconds : 0u,
                                                   .start_tick = tick,
                                                   .flags = known ? JELLI_SLEEP_BED_KNOWN : 0u};
    log->head = (uint8_t)(((unsigned)log->head + 1u) % JELLI_SLEEP_SESSION_CAPACITY);
    if (log->count < JELLI_SLEEP_SESSION_CAPACITY)
        ++log->count;
    log->active = true;
    log->pet_id = pet_id;
    return true;
}

bool jelli_sleep_log_finish(JelliSleepLog *log, uint64_t tick, bool known, uint64_t seconds)
{
    if (!jelli_sleep_log_valid(log) || !log->active)
        return false;
    JelliSleepSession *session = &log->sessions[latest_index(log)];
    uint64_t duration = tick >= session->start_tick ? (tick - session->start_tick) / 10u : 0u;
    session->wake_unix_seconds = known ? seconds : 0u;
    if (known)
        session->flags |= JELLI_SLEEP_WAKE_KNOWN;
    if (known && (session->flags & JELLI_SLEEP_BED_KNOWN) && seconds >= session->bed_unix_seconds) {
        duration = seconds - session->bed_unix_seconds;
        session->flags |= JELLI_SLEEP_REAL_DURATION;
    }
    session->duration_seconds = duration > UINT32_MAX ? UINT32_MAX : (uint32_t)duration;
    log->total_seconds =
        UINT64_MAX - log->total_seconds < duration ? UINT64_MAX : log->total_seconds + duration;
    log->active = false;
    return true;
}

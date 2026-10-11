#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void habits(JelliDebug *debug, const JelliGame *game, uint32_t id)
{
    const JelliPet *pet = &game->pets[game->active];
    const JelliHabits *h = &pet->habits;
    JelliHabitTotals t = jelli_habits_totals(h);
    char body[768];
    int size = snprintf(
        body, sizeof(body),
        "{\"ok\":true,\"pet_id\":%" PRIu32 ",\"rolling\":{\"sleep_seconds\":%" PRIu64
        ",\"play_seconds\":%" PRIu64 ",\"meals_q16\":%" PRIu64 ",\"coverage_seconds\":%" PRIu64
        "},\"lifetime\":{\"sleep_seconds\":%" PRIu64 ",\"play_seconds\":%" PRIu64
        ",\"meals\":%" PRIu32 ",\"coverage_seconds\":%" PRIu64
        "},\"scores\":{\"sleep\":%u,\"food\":%u,\"play\":%u},"
        "\"provisional\":%s,\"clock_known\":%s}",
        pet->id, t.sleep_ticks / 10u, t.play_ticks / 10u, t.meals_q16, t.coverage_ticks / 10u,
        h->lifetime_sleep_ticks / 10u, h->lifetime_play_ticks / 10u, h->lifetime_meals,
        h->observed_ticks / 10u, jelli_habits_sleep_score(h), jelli_habits_food_score(h),
        jelli_habits_play_score(h), t.coverage_ticks < JELLI_HABIT_DAY_TICKS ? "true" : "false",
        game->wall_known ? "true" : "false");
    if (size > 0 && (size_t)size < sizeof(body))
        jelli_debug_response(debug, id, body);
}

static bool append_session(JelliDebug *debug, size_t *used, const JelliSleepSession *s, bool active,
                           bool comma)
{
    int size = snprintf(debug->protocol.reply + *used, sizeof(debug->protocol.reply) - *used,
                        "%s{\"bed_unix_seconds\":%" PRIu64 ",\"wake_unix_seconds\":%" PRIu64
                        ",\"duration_seconds\":%" PRIu32 ",\"flags\":%u,\"active\":%s,"
                        "\"bed_energy\":%u,\"bed_sleep_score\":%u,\"real_duration\":%s}",
                        comma ? "," : "", s->bed_unix_seconds, s->wake_unix_seconds,
                        s->duration_seconds, (unsigned)s->flags, active ? "true" : "false",
                        (unsigned)s->bed_energy, (unsigned)s->bed_sleep_score,
                        (s->flags & JELLI_SLEEP_REAL_DURATION) ? "true" : "false");
    if (size < 0 || (size_t)size >= sizeof(debug->protocol.reply) - *used)
        return false;
    *used += (size_t)size;
    return true;
}

static void sleep_log(JelliDebug *debug, const JelliGame *game, uint32_t id)
{
    const JelliSleepLog *log = &game->sleep_log;
    int size = snprintf(debug->protocol.reply, sizeof(debug->protocol.reply),
                        "\n@J1 %" PRIu32 " {\"ok\":true,\"pet_id\":%" PRIu32
                        ",\"active\":%s,\"total_seconds\":%" PRIu64 ",\"sessions\":[",
                        id, log->pet_id, log->active ? "true" : "false", log->total_seconds);
    if (size < 0 || (size_t)size >= sizeof(debug->protocol.reply))
        return;
    size_t used = (size_t)size;
    for (unsigned i = 0u; i < log->count; ++i) {
        unsigned slot = ((unsigned)log->head + JELLI_SLEEP_SESSION_CAPACITY - log->count + i) %
                        JELLI_SLEEP_SESSION_CAPACITY;
        if (!append_session(debug, &used, &log->sessions[slot], log->active && i + 1u == log->count,
                            i > 0u))
            return;
    }
    if (used + 4u >= sizeof(debug->protocol.reply))
        return;
    memcpy(debug->protocol.reply + used, "]}\n", 4u);
    debug->protocol.reply_size = used + 3u;
}

void jelli_debug_habits(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id,
                        const char *command, unsigned count)
{
    if (count != 3u) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax\"}");
    } else if (!strcmp(command, "habits")) {
        habits(debug, &engine->game, id);
    } else {
        sleep_log(debug, &engine->game, id);
    }
}

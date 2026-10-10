#include "jelli/activities.h"

unsigned jelli_moment_suggested(unsigned hour)
{
    unsigned best = 4u, best_hour = 0u;
    for (unsigned i = 0u; i < jelli_moment_count && i < JELLI_MOMENT_CAPACITY; ++i) {
        const JelliMoment *m = &jelli_moments[i];
        unsigned minute = hour % 24u * 60u;
        if (m->suggest_hour > 23u || m->random_weight || minute < m->start_minute ||
            minute >= m->end_minute)
            continue;
        if (m->suggest_hour <= hour % 24u && m->suggest_hour >= best_hour) {
            best = i;
            best_hour = m->suggest_hour;
        }
    }
    return best < jelli_moment_count ? best : 0u;
}

static unsigned activity_minute(const JelliGame *game, const JelliPet *pet)
{
    if (game->wall_known) {
        int minute =
            (int)(game->wall_seconds / 60u % 1440u) + game->timezone_minutes + game->clock_adjust;
        return (unsigned)((minute % 1440 + 1440) % 1440);
    }
    if (game->clock_known)
        return game->clock_minute;
    int minute = (int)((pet->ticks % JELLI_DAY_TICKS + pet->phase_offset) / 600u) +
                 game->timezone_minutes + game->clock_adjust;
    return (unsigned)((minute % 1440 + 1440) % 1440);
}

uint16_t jelli_activity_day(const JelliGame *game, const JelliPet *pet)
{
    uint64_t day;
    if (game->wall_known) {
        int offset = ((int)game->timezone_minutes + game->clock_adjust) * 60;
        uint64_t seconds = game->wall_seconds;
        if (offset < 0)
            seconds = seconds >= (unsigned)-offset ? seconds - (unsigned)-offset : 0u;
        else if (UINT64_MAX - seconds >= (unsigned)offset)
            seconds += (unsigned)offset;
        day = seconds / 86400u;
    } else {
        /* Divide before converting: even UINT64_MAX ticks fits signed minutes. */
        int64_t minute = (int64_t)(pet->ticks / JELLI_DAY_TICKS) * 1440 +
                         (int64_t)((pet->ticks % JELLI_DAY_TICKS + pet->phase_offset) / 600u) +
                         game->timezone_minutes + game->clock_adjust;
        day = minute > 0 ? (uint64_t)minute / 1440u : 0u;
    }
    return (uint16_t)(day % UINT16_MAX);
}

static bool unlocked(const JelliGame *game, const JelliPet *pet, unsigned id)
{
    const JelliMoment *m = &jelli_moments[id];
    unsigned minute = activity_minute(game, pet);
    if (minute < m->start_minute || minute >= m->end_minute)
        return false;
    if (m->forms && (pet->form >= 32u || !(m->forms & (UINT32_C(1) << pet->form))))
        return false;
    return !m->requires || (pet->activity_day == jelli_activity_day(game, pet) &&
                            (pet->completed_moments & (UINT32_C(1) << (m->requires - 1u))));
}

static unsigned random_choice(const JelliGame *game, const JelliPet *pet)
{
    unsigned total = 0u;
    for (unsigned i = 0u; i < jelli_moment_count; ++i)
        if (unlocked(game, pet, i))
            total += jelli_moments[i].random_weight;
    if (!total)
        return JELLI_MOMENT_CAPACITY;
    /* Stable for the hour, across draws, commands and save/reload. No PRNG side effects. */
    uint32_t seed = pet->id ^ (jelli_activity_day(game, pet) * UINT32_C(2654435761)) ^
                    (activity_minute(game, pet) / 60u * UINT32_C(2246822519));
    seed ^= seed >> 16;
    seed *= UINT32_C(3266489917);
    seed ^= seed >> 13;
    unsigned pick = seed % total;
    for (unsigned i = 0u; i < jelli_moment_count; ++i) {
        if (!unlocked(game, pet, i))
            continue;
        unsigned weight = jelli_moments[i].random_weight;
        if (pick < weight)
            return i;
        pick -= weight;
    }
    return JELLI_MOMENT_CAPACITY;
}

JelliResult jelli_moment_available(const JelliGame *game, const JelliPet *pet, unsigned id)
{
    if (id >= jelli_moment_count || id >= JELLI_MOMENT_CAPACITY)
        return JELLI_INVALID_TARGET;
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (!unlocked(game, pet, id) ||
        (jelli_moments[id].random_weight && random_choice(game, pet) != id))
        return JELLI_NOT_READY;
    return JELLI_OK;
}

void jelli_moment_complete(const JelliGame *game, JelliPet *pet)
{
    if (game->resuming || !pet->moment || pet->moment > jelli_moment_count)
        return;
    uint16_t day = jelli_activity_day(game, pet);
    if (pet->activity_day != day)
        pet->completed_moments = 0u;
    pet->activity_day = day;
    pet->completed_moments |= UINT32_C(1) << (pet->moment - 1u);
}

const char *jelli_moment_hint(const JelliGame *game, const JelliPet *pet, unsigned id)
{
    if (id >= jelli_moment_count)
        return "";
    const JelliMoment *m = &jelli_moments[id];
    if (m->forms && (pet->form >= 32u || !(m->forms & (UINT32_C(1) << pet->form))))
        return "FOR ANOTHER PET";
    unsigned minute = activity_minute(game, pet);
    if (minute < m->start_minute || minute >= m->end_minute)
        return m->window_hint;
    if (m->requires && (pet->activity_day != jelli_activity_day(game, pet) ||
                        !(pet->completed_moments & (UINT32_C(1) << (m->requires - 1u)))))
        return m->prerequisite_hint;
    return m->random_weight ? "CHANGES EACH HOUR" : "TRY AGAIN LATER";
}

#include "game_internal.h"
#include <stddef.h>

_Static_assert(JELLI_NEED_COUNT == 5u, "Event need schema must match game");

JelliEventSnapshot jelli_game_observe(const JelliGame *game, const JelliPet *pet)
{
    JelliEventSnapshot v = {
        .bond = pet->bond,
        .food = game->food,
        .gifts = game->gifts,
        .location = pet->location,
        .health = (uint8_t)pet->health,
        .activity = (uint8_t)pet->activity,
        .flags =
            (uint8_t)((pet->asleep ? 1u : 0u) | (pet->reward_pending ? 2u : 0u) |
                      (pet->reward_claimed ? 4u : 0u) | ((unsigned)pet->reaction << 3) |
                      (game->sleep_log.active && game->sleep_log.pet_id == pet->id ? 32u : 0u))};
    for (unsigned i = 0; i < JELLI_NEED_COUNT; ++i)
        v.needs[i] = pet->needs[i];
    return v;
}

void jelli_game_emit(JelliGame *game, JelliEventKind kind, unsigned code, JelliResult result,
                     uint32_t value, const JelliPet *pet, JelliEventSnapshot before)
{
    if (!game->events)
        return;
    JelliEvent event = {.tick = pet->ticks,
                        .pet_id = pet->id,
                        .value = value,
                        .before = before,
                        .after = jelli_game_observe(game, pet),
                        .kind = (uint8_t)kind,
                        .code = (uint8_t)code,
                        .result = (uint8_t)result};
    jelli_events_push(game->events, event);
}

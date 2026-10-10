#include "jelli/behavior.h"
#include "game_internal.h"
#include "jelli/collection.h"

#include <stddef.h>

bool jelli_prize_progress_valid(const JelliPrizeProgress *progress)
{
    if (!progress || (!progress->breakfast_day_known && progress->last_breakfast_day != 0u))
        return false;
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i) {
        if (progress->counts[i] > 3u)
            return false;
    }
    return progress->counts[7] <= 1u;
}

bool jelli_prizes_valid(const JelliPrizes *prizes)
{
    if (!prizes || prizes->owned > JELLI_PRIZE_MASK || prizes->discovered > JELLI_PRIZE_MASK ||
        (prizes->owned & prizes->discovered) != prizes->owned ||
        prizes->offered > JELLI_PRIZE_COUNT ||
        (prizes->offered == 0u) != (prizes->offered_pet == 0u))
        return false;
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i) {
        bool discovered = (prizes->discovered & (1u << i)) != 0u;
        if (discovered != (prizes->origin_pet[i] != 0u))
            return false;
    }
    return !prizes->offered || !(prizes->owned & (1u << (prizes->offered - 1u)));
}

static bool offer(JelliGame *game, unsigned index)
{
    if (game->prizes.offered || (game->prizes.owned & (1u << index)))
        return false;
    game->prizes.offered = (uint8_t)(index + 1u);
    game->prizes.offered_pet = game->pets[game->active].id;
    jelli_behavior_stimulus(game, JELLI_STIM_PRESENT_OFFERED, index);
    return true;
}

static bool count_completion(JelliPet *pet, unsigned index)
{
    if (pet->prize_progress.counts[index] < 3u)
        ++pet->prize_progress.counts[index];
    return pet->prize_progress.counts[index] == 3u;
}

static uint64_t breakfast_day(const JelliGame *game, const JelliPet *pet)
{
    if (!game->wall_known)
        return pet->ticks / JELLI_DAY_TICKS +
               (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset) / JELLI_DAY_TICKS;
    uint64_t day = game->wall_seconds / 86400u;
    int32_t seconds = (int32_t)(game->wall_seconds % 86400u) +
                      ((int32_t)game->timezone_minutes + game->clock_adjust) * 60;
    if (seconds < 0)
        return day ? day - 1u : 0u;
    return day + (unsigned)seconds / 86400u;
}

static void breakfast(JelliGame *game, JelliPet *pet)
{
    unsigned minute = game->clock_known
                          ? game->clock_minute
                          : (unsigned)((pet->ticks % JELLI_DAY_TICKS + pet->phase_offset) %
                                       JELLI_DAY_TICKS / 600u);
    if (minute >= 660u)
        return;
    uint64_t day = breakfast_day(game, pet);
    JelliPrizeProgress *p = &pet->prize_progress;
    if (p->breakfast_day_known && day <= p->last_breakfast_day)
        return;
    p->last_breakfast_day = day;
    p->breakfast_day_known = true;
    if (count_completion(pet, 2u))
        offer(game, 2u);
}

static void garden(JelliGame *game, JelliPet *pet, bool outing)
{
    if (pet->location != 1u)
        return;
    if (pet->prize_progress.counts[7]) {
        if (offer(game, 7u))
            pet->prize_progress.counts[7] = 0u;
    }
    if (outing) {
        (void)count_completion(pet, 0u);
        pet->random_state = pet->random_state * UINT32_C(1664525) + UINT32_C(1013904223);
        if (!pet->random_state)
            pet->random_state = 1u;
        if (pet->random_state % 3u == 0u)
            offer(game, 0u);
    }
}

void jelli_prize_complete(JelliGame *game, unsigned trigger)
{
    if (!jelli_game_valid(game) || game->resuming)
        return;
    JelliPet *pet = &game->pets[game->active];
    switch (trigger) {
    case JELLI_PRIZE_BRUSH:
        if (count_completion(pet, 1u))
            offer(game, 1u);
        break;
    case JELLI_PRIZE_WASH:
        offer(game, 5u);
        break;
    case JELLI_PRIZE_BREAKFAST:
        breakfast(game, pet);
        break;
    case JELLI_PRIZE_TEA:
        if (count_completion(pet, 3u))
            offer(game, 3u);
        break;
    case JELLI_PRIZE_MOVIE:
        if (!pet->asleep && pet->needs[JELLI_ENERGY] >= 400u)
            offer(game, 4u);
        break;
    case JELLI_PRIZE_OUTING:
    case JELLI_PRIZE_TRAVEL:
        garden(game, pet, trigger == JELLI_PRIZE_OUTING);
        break;
    case JELLI_PRIZE_CARE:
        pet->prize_progress.counts[7] = 1u;
        break;
    case JELLI_PRIZE_SLEEP:
        offer(game, 6u);
        break;
    case JELLI_PRIZE_GIFT:
        offer(game, 8u);
        break;
    default:
        break;
    }
}

JelliResult jelli_prize_catch(JelliGame *game)
{
    if (!jelli_game_valid(game))
        return JELLI_INVALID_TARGET;
    if (game->resuming)
        return JELLI_BUSY;
    if (!game->prizes.offered)
        return JELLI_NOT_READY;
    unsigned index = game->prizes.offered - 1u;
    game->prizes.owned |= (uint16_t)(1u << index);
    game->prizes.discovered |= (uint16_t)(1u << index);
    /* Discovery persists, but each newly earned copy belongs to its catcher.
     * A later copy must not inherit permission to gift itself from an old one. */
    game->prizes.origin_pet[index] = game->prizes.offered_pet;
    game->prizes.offered = 0u;
    game->prizes.offered_pet = 0u;
    jelli_collection_unlock(game);
    jelli_behavior_stimulus(game, JELLI_STIM_PRESENT_CAUGHT, index);
    return JELLI_OK;
}

static JelliResult gift_result(const JelliGame *game, unsigned index)
{
    if (index >= JELLI_PRIZE_COUNT)
        return JELLI_INVALID_TARGET;
    if (game->resuming)
        return JELLI_BUSY;
    if (!(game->prizes.owned & (1u << index)))
        return JELLI_NO_ITEM;
    const JelliPet *pet = &game->pets[game->active];
    if (pet->id == game->prizes.origin_pet[index])
        return JELLI_NOT_READY;
    if (pet->asleep)
        return JELLI_ASLEEP;
    return pet->activity == JELLI_IDLE ? JELLI_OK : JELLI_BUSY;
}

JelliResult jelli_prize_gift(JelliGame *game, unsigned index)
{
    if (!jelli_game_valid(game))
        return JELLI_INVALID_TARGET;
    JelliPet *pet = &game->pets[game->active];
    JelliEventSnapshot before = jelli_game_observe(game, pet);
    JelliResult result = gift_result(game, index);
    if (result == JELLI_OK) {
        game->prizes.owned &= (uint16_t)~(1u << index);
        unsigned social = pet->needs[JELLI_SOCIAL] + jelli_habits_social_gain(&pet->habits, 80u);
        pet->needs[JELLI_SOCIAL] = social > 1000u ? 1000u : (uint16_t)social;
        pet->need_remainders[JELLI_SOCIAL] = 0u;
        pet->bond = pet->bond > 970u ? 1000u : (uint16_t)(pet->bond + 30u);
        /* A bow cannot manufacture its own replacement by being gifted. */
        if (index != 8u)
            jelli_prize_complete(game, JELLI_PRIZE_GIFT);
        jelli_behavior_stimulus(game, JELLI_STIM_PRESENT_GIVEN, index);
    }
    unsigned value = index < JELLI_PRIZE_COUNT ? index + 1u : 0u;
    jelli_game_emit(game, JELLI_EVENT_COMMAND, JELLI_CMD_GIFT, result, value, pet, before);
    return result;
}

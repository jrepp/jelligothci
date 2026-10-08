#include "game_internal.h"

#include <limits.h>
#include <stddef.h>

static uint8_t find_pet(const JelliGame *game, uint32_t id)
{
    for (uint8_t i = 0u; i < game->count; ++i) {
        if (game->pets[i].id == id)
            return i;
    }
    return JELLI_PET_CAPACITY;
}

static bool begin_activity(JelliPet *pet, JelliActivity activity, uint64_t duration)
{
    if (UINT64_MAX - pet->ticks < duration)
        return false;
    pet->activity = activity;
    pet->interaction_due = pet->ticks + duration;
    return true;
}

static uint64_t deadline_after(const JelliPet *pet, uint64_t duration)
{
    return UINT64_MAX - pet->ticks < duration ? UINT64_MAX : pet->ticks + duration;
}

static JelliResult claim_reward(JelliGame *game, JelliPet *pet)
{
    if (!pet->reward_pending || pet->reward_claimed)
        return JELLI_NOT_READY;
    if ((uint32_t)game->food + 3u > JELLI_STACK_LIMIT)
        return JELLI_FULL;
    game->food = (uint16_t)(game->food + 3u);
    pet->reward_pending = false;
    pet->reward_claimed = true;
    return JELLI_OK;
}

static JelliResult activate_pet(JelliGame *game, JelliCommand command, const JelliPet *active)
{
    uint8_t target_index = find_pet(game, command.value);
    if (target_index >= game->count)
        return JELLI_INVALID_TARGET;
    if (game->resuming || active->activity != JELLI_IDLE)
        return JELLI_NOT_READY;
    game->active = target_index;
    return JELLI_OK;
}

static JelliResult start_care(JelliPet *pet)
{
    if (pet->activity == JELLI_CARING)
        return JELLI_BUSY;
    if (UINT64_MAX - pet->ticks < 300u)
        return JELLI_NOT_READY;
    pet->activity = JELLI_IDLE;
    pet->interaction_due = 0u;
    pet->asleep = false;
    pet->scheduled_sleep = false;
    pet->nap_due = 0u;
    for (size_t i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (pet->needs[i] <= 400u) {
            pet->needs[i] = 400u;
            pet->need_remainders[i] = 0u;
        }
    }
    pet->health = JELLI_RECOVERING;
    (void)begin_activity(pet, JELLI_CARING, 300u);
    return JELLI_OK;
}

static bool bedtime_pending(const JelliPet *pet)
{
    return jelli_game_window(pet, NULL) && pet->ticks >= pet->wake_override_until;
}

static JelliResult start_feed(const JelliGame *game, JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (bedtime_pending(pet))
        return JELLI_BUSY;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (game->food == 0u)
        return JELLI_NO_ITEM;
    return begin_activity(pet, JELLI_EATING, 50u) ? JELLI_OK : JELLI_NOT_READY;
}

static JelliResult start_play(JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (bedtime_pending(pet))
        return JELLI_BUSY;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    return begin_activity(pet, JELLI_PLAYING, 80u) ? JELLI_OK : JELLI_NOT_READY;
}

static JelliResult start_clean(JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    return begin_activity(pet, JELLI_CLEANING, 50u) ? JELLI_OK : JELLI_NOT_READY;
}

static JelliResult start_gift(const JelliGame *game, JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (bedtime_pending(pet))
        return JELLI_BUSY;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (game->gifts == 0u)
        return JELLI_NO_ITEM;
    return begin_activity(pet, JELLI_GIVING, 50u) ? JELLI_OK : JELLI_NOT_READY;
}

static JelliResult rest(JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (UINT64_MAX - pet->ticks < UINT64_C(36000))
        return JELLI_NOT_READY;
    pet->asleep = true;
    pet->scheduled_sleep = false;
    pet->nap_due = deadline_after(pet, UINT64_C(36000));
    return JELLI_OK;
}

static JelliResult wake(JelliPet *pet)
{
    if (!pet->asleep)
        return JELLI_NOT_READY;
    uint64_t remaining = 0u;
    if (jelli_game_window(pet, &remaining))
        pet->wake_override_until = deadline_after(pet, remaining);
    pet->asleep = false;
    pet->scheduled_sleep = false;
    pet->nap_due = 0u;
    pet->awake_until = deadline_after(pet, UINT64_C(36000));
    if (pet->needs[JELLI_SATIETY] <= 200u && !pet->hunger_low) {
        pet->hunger_low = true;
        pet->hunger_counted = false;
    }
    if (pet->hunger_low && !pet->hunger_counted)
        pet->hunger_due = deadline_after(pet, UINT64_C(6000));
    return JELLI_OK;
}

static JelliResult travel(JelliPet *pet, uint32_t location)
{
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (location > 1u)
        return JELLI_INVALID_TARGET;
    pet->location = (uint8_t)location;
    return JELLI_OK;
}

static JelliResult set_bedtime(JelliPet *pet, uint32_t hour)
{
    if (hour > 23u)
        return JELLI_INVALID_TARGET;
    pet->bedtime = hour;
    return JELLI_OK;
}

static JelliResult dispatch_action(JelliGame *game, JelliCommand command, JelliPet *pet)
{
    switch (command.kind) {
    case JELLI_CMD_FEED:
        return start_feed(game, pet);
    case JELLI_CMD_PLAY:
        return start_play(pet);
    case JELLI_CMD_CLEAN:
        return start_clean(pet);
    case JELLI_CMD_CARE:
        return start_care(pet);
    case JELLI_CMD_GIFT:
        return start_gift(game, pet);
    case JELLI_CMD_CLAIM:
        return pet->activity == JELLI_IDLE ? claim_reward(game, pet) : JELLI_BUSY;
    case JELLI_CMD_REST:
        return rest(pet);
    case JELLI_CMD_WAKE:
        return wake(pet);
    case JELLI_CMD_TRAVEL:
        return travel(pet, command.value);
    case JELLI_CMD_BEDTIME:
        return set_bedtime(pet, command.value);
    case JELLI_CMD_ACTIVATE:
        return activate_pet(game, command, pet);
    }
    return JELLI_INVALID_TARGET;
}

JelliResult jelli_game_command(JelliGame *game, JelliCommand command)
{
    if (!jelli_game_valid(game))
        return JELLI_INVALID_TARGET;
    if (game->resuming)
        return JELLI_BUSY;
    if (command.kind == JELLI_CMD_ACTIVATE) {
        const JelliPet *active = &game->pets[game->active];
        if (active->id != command.actor_id)
            return JELLI_INVALID_TARGET;
        return activate_pet(game, command, active);
    }
    JelliPet *pet = &game->pets[game->active];
    if (pet->id != command.actor_id)
        return JELLI_INVALID_TARGET;
    return dispatch_action(game, command, pet);
}

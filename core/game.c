#include "jelli/game.h"

#include <limits.h>
#include <stddef.h>

static bool enum_values_valid(const JelliPet *pet)
{
    switch (pet->health) {
    case JELLI_WELL:
    case JELLI_UNWELL:
    case JELLI_RECOVERING:
        break;
    default:
        return false;
    }
    switch (pet->activity) {
    case JELLI_IDLE:
    case JELLI_EATING:
    case JELLI_PLAYING:
    case JELLI_CLEANING:
    case JELLI_CARING:
    case JELLI_GIVING:
        return true;
    default:
        return false;
    }
}

void jelli_game_init(JelliGame *game)
{
    if (game == NULL)
        return;
    *game = (JelliGame){0};
    game->count = 2u;
    game->food = 5u;
    game->gifts = 3u;
    game->pets[0] = (JelliPet){.id = 1u,
                               .phase_offset = 324000u,
                               .bedtime = 22u,
                               .sleep_duration = 288000u,
                               .random_state = 1u,
                               .needs = {500u, 700u, 700u, 500u, 500u},
                               .bond = 100u,
                               .health = JELLI_WELL,
                               .activity = JELLI_IDLE};
    game->pets[1] = (JelliPet){.id = 2u,
                               .phase_offset = 324000u,
                               .bedtime = 22u,
                               .sleep_duration = 288000u,
                               .random_state = 2u,
                               .needs = {500u, 700u, 700u, 500u, 500u},
                               .bond = 100u,
                               .health = JELLI_WELL,
                               .activity = JELLI_IDLE};
}

static bool pet_profile_valid(const JelliPet *pet)
{
    if (pet->id == 0u || pet->bedtime >= 24u || pet->sleep_duration == 0u ||
        pet->sleep_duration > JELLI_DAY_TICKS - 600u || pet->sleep_duration < 600u ||
        pet->phase_offset >= JELLI_DAY_TICKS || pet->random_state == 0u || pet->form > 1u ||
        pet->location > 1u || pet->bond > 1000u || !enum_values_valid(pet))
        return false;
    return true;
}

static bool pet_needs_valid(const JelliPet *pet)
{
    for (size_t i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (pet->needs[i] > 1000u || pet->need_remainders[i] >= 600u)
            return false;
    }
    return true;
}

static bool pet_activity_valid(const JelliPet *pet)
{
    if (pet->activity == JELLI_IDLE && pet->interaction_due != 0u)
        return false;
    if (pet->activity != JELLI_IDLE && pet->interaction_due < pet->ticks)
        return false;
    if (pet->health == JELLI_RECOVERING && pet->activity != JELLI_CARING)
        return false;
    if (pet->activity == JELLI_CARING && pet->health != JELLI_RECOVERING)
        return false;
    return true;
}

static bool pet_sleep_valid(const JelliPet *pet)
{
    if (pet->asleep != (pet->scheduled_sleep || pet->nap_due != 0u))
        return false;
    if ((pet->scheduled_sleep && pet->nap_due != 0u) ||
        (pet->asleep && !pet->scheduled_sleep && pet->nap_due <= pet->ticks))
        return false;
    return true;
}

static bool pet_recovery_valid(const JelliPet *pet)
{
    if (pet->health != JELLI_RECOVERING)
        return true;
    for (size_t i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (pet->needs[i] < 400u || (pet->needs[i] == 400u && pet->need_remainders[i] != 0u))
            return false;
    }
    return true;
}

static bool pet_lifecycle_valid(const JelliPet *pet)
{
    if ((pet->reward_pending && pet->reward_claimed) ||
        (pet->form == 0u && pet->stage_ticks >= 600u) ||
        (pet->hunger_counted && !pet->hunger_low) ||
        (!pet->hunger_low && (pet->hunger_counted || pet->hunger_due != 0u)))
        return false;
    return pet_sleep_valid(pet) && pet_recovery_valid(pet);
}

static bool pet_valid(const JelliPet *pet)
{
    return pet_profile_valid(pet) && pet_needs_valid(pet) && pet_activity_valid(pet) &&
           pet_lifecycle_valid(pet);
}

static bool game_header_valid(const JelliGame *game)
{
    return game != NULL && game->count >= 2u && game->count <= JELLI_PET_CAPACITY &&
           game->active < game->count && game->food <= JELLI_STACK_LIMIT &&
           game->gifts <= JELLI_STACK_LIMIT && game->backlog_ms <= 2000u &&
           game->resume_remaining_ms <= JELLI_OFFLINE_CAP_MS &&
           (!game->resuming || game->resume_remaining_ms != 0u) &&
           (game->resuming || game->resume_remaining_ms == 0u);
}

static bool pet_reservation_valid(const JelliGame *game, uint8_t index)
{
    const JelliPet *pet = &game->pets[index];
    if (index != game->active)
        return pet->activity == JELLI_IDLE;
    if (pet->asleep && pet->activity != JELLI_IDLE)
        return false;
    if (pet->activity == JELLI_EATING && game->food == 0u)
        return false;
    if (pet->activity == JELLI_GIVING && game->gifts == 0u)
        return false;
    return true;
}

static bool game_pets_valid(const JelliGame *game)
{
    for (uint8_t i = 0u; i < game->count; ++i) {
        if (!pet_valid(&game->pets[i]) || !pet_reservation_valid(game, i))
            return false;
        for (uint8_t j = (uint8_t)(i + 1u); j < game->count; ++j) {
            if (game->pets[i].id == game->pets[j].id)
                return false;
        }
    }
    for (uint8_t i = game->count; i < JELLI_PET_CAPACITY; ++i) {
        if (game->pets[i].id != 0u)
            return false;
    }
    return true;
}

bool jelli_game_valid(const JelliGame *game)
{
    return game_header_valid(game) && game_pets_valid(game);
}

const char *jelli_game_result_name(JelliResult result)
{
    switch (result) {
    case JELLI_OK:
        return "ok";
    case JELLI_BUSY:
        return "busy";
    case JELLI_NO_ITEM:
        return "no item";
    case JELLI_ASLEEP:
        return "asleep";
    case JELLI_INVALID_TARGET:
        return "invalid target";
    case JELLI_FULL:
        return "full";
    case JELLI_NOT_READY:
        return "not ready";
    default:
        return "invalid result";
    }
}

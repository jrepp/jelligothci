#include "jelli/locations.h"
#include "jelli/wake.h"
#include "jelli/game.h"
#include "jelli/collection.h"
#include "jelli/nutrition.h"
#include "jelli/activities.h"
#include "jelli/behavior.h"

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
    case JELLI_EXERCISING:
        return true;
    default:
        return false;
    }
}

void jelli_game_init(JelliGame *game)
{
    if (game == NULL)
        return;
    *game = (JelliGame){.volume = JELLI_VOLUME_DEFAULT};
    game->count = 1u;
    game->food = jelli_collection_start.food;
    game->gifts = jelli_collection_start.gifts;
    game->pets[0] = jelli_collection_new_pet(1u, 1u);
    jelli_collection_unlock(game);
    game->new_pets = 0u;
}

/* Running moment, potty cycle (save version 10) and behaviour state (version 11). */
/* Longest reaction any species' touch or a wake can start. */
static unsigned reaction_ticks_limit(void)
{
    return jelli_behavior_touch_ticks_max > jelli_wake_rules.reaction_ticks
               ? jelli_behavior_touch_ticks_max
               : jelli_wake_rules.reaction_ticks;
}

static bool pet_routine_valid(const JelliPet *pet)
{
    return pet->moment <= jelli_moment_count && pet->digesting <= 1000u && pet->potty <= 1000u &&
           (pet->moment == 0u ||
            (pet->activity == JELLI_PLAYING || pet->activity == JELLI_EATING)) &&
           pet->behavior <= jelli_behavior_state_count &&
           pet->cooldown_state <= jelli_behavior_state_count &&
           (pet->behavior == 0u) == !pet->behavior_left;
}

static bool pet_profile_valid(const JelliPet *pet)
{
    if (pet->id == 0u || pet->bedtime >= 24u || pet->sleep_duration == 0u ||
        pet->sleep_duration > JELLI_DAY_TICKS - 600u || pet->sleep_duration < 600u ||
        pet->phase_offset >= JELLI_DAY_TICKS || pet->random_state == 0u ||
        pet->form >= jelli_collection_form_count || pet->food_type >= jelli_food_count ||
        pet->hydration > 1000u || pet->hydration_remainder >= 2400u ||
        pet->location >= jelli_location_count || pet->bond > 1000u ||
        pet->wake_mood > JELLI_WAKE_HAPPY || pet->rest_ticks > jelli_wake_rules.sleep_ticks ||
        pet->touch_load > 1000u || pet->reaction > JELLI_REACTION_TOUCH_OVERLOAD ||
        pet->reaction_ticks > reaction_ticks_limit() || !enum_values_valid(pet))
        return false;
    return pet_routine_valid(pet);
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
        (pet->asleep && !pet->scheduled_sleep && pet->nap_due <= pet->ticks &&
         pet->ticks != UINT64_MAX))
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
    if ((pet->reward_pending && pet->reward_claimed) || jelli_collection_growth_due(pet) ||
        (pet->hunger_counted && !pet->hunger_low) ||
        (!pet->hunger_low && (pet->hunger_counted || pet->hunger_due != 0u)))
        return false;
    return pet_sleep_valid(pet) && pet_recovery_valid(pet);
}

static bool pet_valid(const JelliPet *pet)
{
    return jelli_prize_progress_valid(&pet->prize_progress) && jelli_habits_valid(&pet->habits) &&
           pet->habits.cursor_ticks <= pet->ticks && pet->shot_goal <= 3u &&
           pet->shot_hits <= pet->shot_goal &&
           (pet->shot_goal == 0u ? pet->shot_until == 0u : pet->shot_until > 0u) &&
           pet_profile_valid(pet) && pet_needs_valid(pet) && pet_activity_valid(pet) &&
           pet_lifecycle_valid(pet);
}

static bool game_header_valid(const JelliGame *game)
{
    return game != NULL && jelli_sleep_log_valid(&game->sleep_log) &&
           jelli_prizes_valid(&game->prizes) && game->count >= 1u &&
           game->count <= JELLI_PET_CAPACITY && game->active < game->count &&
           game->food <= JELLI_STACK_LIMIT && game->gifts <= JELLI_STACK_LIMIT &&
           game->backlog_ms <= 2000u && game->resume_remaining_ms <= JELLI_OFFLINE_CAP_MS &&
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
    if (game->sleep_log.active) {
        unsigned last = ((unsigned)game->sleep_log.head + JELLI_SLEEP_SESSION_CAPACITY - 1u) %
                        JELLI_SLEEP_SESSION_CAPACITY;
        if (game->sleep_log.pet_id != game->pets[game->active].id ||
            game->sleep_log.sessions[last].start_tick > game->pets[game->active].ticks)
            return false;
    }
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

static bool prize_pet_known(const JelliGame *game, uint32_t id)
{
    if (!id)
        return true;
    for (unsigned i = 0u; i < game->count; ++i) {
        if (game->pets[i].id == id)
            return true;
    }
    return false;
}

static bool prize_sources_valid(const JelliGame *game)
{
    if (!prize_pet_known(game, game->prizes.offered_pet))
        return false;
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i) {
        if (!prize_pet_known(game, game->prizes.origin_pet[i]))
            return false;
    }
    return true;
}

bool jelli_game_valid(const JelliGame *game)
{
    if (game && game->volume > JELLI_VOLUME_MAX)
        return false;
    return game_header_valid(game) && game_pets_valid(game) && prize_sources_valid(game) &&
           jelli_collection_valid(game);
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

#include "game_internal.h"
#include "jelli/locations.h"
#include "jelli/activities.h"

JelliResult jelli_game_travel(JelliPet *pet, uint32_t location)
{
    if (location >= jelli_location_count)
        return JELLI_INVALID_TARGET;
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->location == location)
        return JELLI_NOT_READY;
    if (pet->health == JELLI_RECOVERING)
        pet->health = JELLI_UNWELL;
    pet->activity = JELLI_IDLE;
    pet->interaction_due = 0u;
    pet->moment = 0u;
    pet->behavior_flags &= (uint8_t)~JELLI_PET_FLAG_FREE_PLAY;
    jelli_activity_rewards_clear(pet);
    pet->location = (uint8_t)location;
    return JELLI_OK;
}

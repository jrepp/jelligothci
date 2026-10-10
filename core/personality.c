#include "game_internal.h"

static uint16_t adjusted(uint16_t value, int delta, unsigned floor)
{
    int result = (int)value + delta;
    if (result < (int)floor)
        return (uint16_t)floor;
    return result > 1000 ? 1000u : (uint16_t)result;
}

unsigned jelli_pet_mood(const JelliPet *pet)
{
    unsigned comfort = (unsigned)(pet->needs[0] + pet->needs[1] + pet->needs[2]) / 3u;
    unsigned joy = ((unsigned)pet->needs[3] * 3u + comfort) / 4u;
    unsigned connection = ((unsigned)pet->needs[4] * 2u + pet->bond) / 3u;
    unsigned mood = (comfort * 2u + joy + connection) / 4u;
    if (pet->health == JELLI_UNWELL && mood > 400u)
        mood = 400u;
    if (pet->reaction >= JELLI_REACTION_TOUCH_UPSET)
        mood = mood > 150u ? mood - 150u : 0u;
    return mood ? (mood + 9u) / 10u : 1u;
}

unsigned jelli_pet_favorite(const JelliPet *pet, unsigned minute)
{
    /* Stable identity survives evolution. Morning companions enjoy breakfast/tea;
     * evening companions favor outings/movies. Balance is a prototype profile. */
    if (pet->id & 1u)
        return minute < 660u ? 0u : 1u;
    return minute < 1140u ? 2u : 3u;
}

void jelli_game_preference(JelliGame *game, JelliCommand command)
{
    JelliPet *pet = &game->pets[game->active];
    unsigned minute = game->clock_known
                          ? game->clock_minute
                          : (unsigned)((pet->ticks % JELLI_DAY_TICKS + pet->phase_offset) %
                                       JELLI_DAY_TICKS / 600u);
    if (command.kind != JELLI_CMD_MOMENT || command.value != jelli_pet_favorite(pet, minute))
        return;
    unsigned bonus = ((pet->id & 1u) ? minute < 900u : minute >= 900u) ? 60u : 30u;
    pet->needs[JELLI_AMUSEMENT] = adjusted(pet->needs[JELLI_AMUSEMENT],
                                           (int)jelli_habits_social_gain(&pet->habits, bonus), 0u);
    pet->needs[JELLI_SOCIAL] = adjusted(
        pet->needs[JELLI_SOCIAL], (int)jelli_habits_social_gain(&pet->habits, bonus / 2u), 0u);
    pet->bond = adjusted(pet->bond, 10, 0u);
}

JelliResult jelli_game_touch(JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    pet->wake_mood = 0u;
    pet->touch_load = adjusted(pet->touch_load, 220, 0u);
    pet->reaction = pet->touch_load >= 900u   ? JELLI_REACTION_TOUCH_OVERLOAD
                    : pet->touch_load >= 600u ? JELLI_REACTION_TOUCH_UPSET
                                              : JELLI_REACTION_TOUCH_HAPPY;
    pet->reaction_ticks = 30u;
    unsigned floor = pet->health == JELLI_RECOVERING ? 400u : 0u;
    int change = pet->reaction == JELLI_REACTION_TOUCH_HAPPY
                     ? (int)jelli_habits_social_gain(&pet->habits, 15u)
                     : -20;
    pet->needs[JELLI_SOCIAL] = adjusted(pet->needs[JELLI_SOCIAL], change, floor);
    pet->needs[JELLI_AMUSEMENT] = adjusted(pet->needs[JELLI_AMUSEMENT], change, floor);
    if (pet->reaction == JELLI_REACTION_TOUCH_HAPPY)
        pet->bond = adjusted(pet->bond, 3, 0u);
    return JELLI_OK;
}

void jelli_pet_touch_decay(JelliPet *pet, uint64_t ticks)
{
    pet->touch_load = ticks >= 100u || ticks * 10u >= pet->touch_load
                          ? 0u
                          : (uint16_t)(pet->touch_load - ticks * 10u);
    pet->reaction_ticks =
        ticks >= pet->reaction_ticks ? 0u : (uint8_t)(pet->reaction_ticks - ticks);
    if (!pet->reaction_ticks) {
        pet->reaction = 0u;
        pet->wake_mood = 0u;
    }
}

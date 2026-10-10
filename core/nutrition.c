#include "jelli/potty.h"
#include "jelli/activities.h"
#include "jelli/nutrition.h"

void jelli_hydration_add(JelliPet *pet, unsigned amount)
{
    unsigned room = 1000u - pet->hydration;
    pet->hydration = amount >= room ? 1000u : (uint16_t)(pet->hydration + amount);
    pet->hydration_remainder = 0u;
}

void jelli_hydration_advance(JelliPet *pet, uint64_t ticks)
{
    unsigned rate = pet->asleep ? 1u : 4u;
    uint64_t fraction = ticks % 2400u * rate + pet->hydration_remainder;
    uint64_t loss = ticks / 2400u * rate + fraction / 2400u;
    if (loss >= pet->hydration) {
        pet->hydration = pet->hydration_remainder = 0u;
        return;
    }
    pet->hydration -= (uint16_t)loss;
    pet->hydration_remainder = (uint16_t)(fraction % 2400u);
}

JelliResult jelli_drink_water(JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (pet->hydration == 1000u)
        return JELLI_FULL;
    jelli_hydration_add(pet, 1000u);
    jelli_potty_drink(pet);
    return JELLI_OK;
}

JelliResult jelli_start_exercise(JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    unsigned energy = jelli_activity_cost(pet, JELLI_ENERGY, jelli_exercise.energy_cost);
    unsigned hydration =
        jelli_activity_cost(pet, JELLI_ACTIVITY_HYDRATION, jelli_exercise.hydration_cost);
    if (pet->needs[JELLI_SATIETY] < jelli_exercise.fullness_cost ||
        pet->needs[JELLI_ENERGY] < energy || pet->hydration < hydration ||
        UINT64_MAX - pet->ticks < jelli_exercise.duration_ticks)
        return JELLI_NOT_READY;
    pet->needs[JELLI_SATIETY] -= jelli_exercise.fullness_cost;
    pet->needs[JELLI_ENERGY] -= (uint16_t)energy;
    pet->hydration -= (uint16_t)hydration;
    pet->activity = JELLI_EXERCISING;
    pet->interaction_due = pet->ticks + jelli_exercise.duration_ticks;
    return JELLI_OK;
}

JelliResult jelli_refill_food(JelliGame *game)
{
    if (game->food)
        return JELLI_FULL;
    game->food = 5u;
    ++game->revision;
    return JELLI_OK;
}

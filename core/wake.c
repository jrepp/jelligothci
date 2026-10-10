#include "jelli/wake.h"

void jelli_wake_advance(JelliPet *pet, uint64_t ticks)
{
    if (!pet->asleep) {
        pet->rest_ticks = 0u;
        return;
    }
    uint32_t cap = jelli_wake_rules.sleep_ticks;
    uint32_t room = cap - pet->rest_ticks;
    pet->rest_ticks += ticks > room ? room : (uint32_t)ticks;
}

void jelli_wake_react(JelliPet *pet)
{
    uint32_t needed =
        pet->scheduled_sleep ? jelli_wake_rules.sleep_ticks : jelli_wake_rules.nap_ticks;
    bool happy = pet->asleep && pet->rest_ticks >= needed;
    pet->wake_mood = (uint8_t)(happy ? JELLI_WAKE_HAPPY : JELLI_WAKE_GROGGY);
    pet->reaction = 0u;
    pet->reaction_ticks = jelli_wake_rules.reaction_ticks;
    pet->rest_ticks = 0u;
    if (happy) {
        uint32_t bond = pet->bond + jelli_wake_rules.bond_gain;
        pet->bond = (uint16_t)(bond > 1000u ? 1000u : bond);
    }
}

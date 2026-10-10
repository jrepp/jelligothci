#include "jelli/potty.h"

#define POTTY_MINUTE_TICKS UINT64_C(600)
#define POTTY_MAX 1000u

static uint16_t add_capped(uint16_t value, unsigned amount)
{
    unsigned sum = (unsigned)value + amount;
    return (uint16_t)(sum > POTTY_MAX ? POTTY_MAX : sum);
}

void jelli_potty_eat(JelliPet *pet)
{
    pet->digesting = add_capped(pet->digesting, jelli_potty_rules.per_meal);
}

void jelli_potty_drink(JelliPet *pet)
{
    pet->digesting = add_capped(pet->digesting, jelli_potty_rules.per_drink);
}

bool jelli_potty_advance(JelliPet *pet, uint64_t old_ticks)
{
    uint64_t minutes = pet->ticks / POTTY_MINUTE_TICKS - old_ticks / POTTY_MINUTE_TICKS;
    if (minutes == 0u || pet->digesting == 0u)
        return false;
    uint64_t moved = minutes * jelli_potty_rules.drain_per_minute;
    if (moved > pet->digesting)
        moved = pet->digesting;
    bool below = pet->potty < jelli_potty_rules.urge_threshold;
    pet->digesting = (uint16_t)(pet->digesting - moved);
    pet->potty = add_capped(pet->potty, (unsigned)moved);
    return below && pet->potty >= jelli_potty_rules.urge_threshold;
}

JelliResult jelli_potty_break(JelliPet *pet)
{
    if (pet->potty < jelli_potty_rules.minimum_to_go)
        return JELLI_FULL;
    pet->potty = 0u;
    pet->needs[JELLI_HYGIENE] =
        add_capped(pet->needs[JELLI_HYGIENE], jelli_potty_rules.hygiene_gain);
    pet->bond = add_capped(pet->bond, jelli_potty_rules.bond_gain);
    return JELLI_OK;
}

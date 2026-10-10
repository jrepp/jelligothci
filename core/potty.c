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

static uint16_t drop(uint16_t value, uint64_t amount)
{
    return (uint16_t)(value > amount ? value - amount : 0u);
}

bool jelli_potty_advance(JelliPet *pet, uint64_t old_ticks)
{
    uint64_t minutes = pet->ticks / POTTY_MINUTE_TICKS - old_ticks / POTTY_MINUTE_TICKS;
    if (minutes == 0u)
        return false;
    if (pet->behavior_flags & JELLI_PET_FLAG_MESS)
        pet->needs[JELLI_HYGIENE] =
            drop(pet->needs[JELLI_HYGIENE], minutes * jelli_potty_rules.mess_hygiene_per_minute);
    if (pet->digesting == 0u)
        return false;
    uint64_t moved = minutes * jelli_potty_rules.drain_per_minute;
    if (moved > pet->digesting)
        moved = pet->digesting;
    bool below = pet->potty < jelli_potty_rules.urge_threshold;
    pet->digesting = (uint16_t)(pet->digesting - moved);
    pet->potty = add_capped(pet->potty, (unsigned)moved);
    return below && pet->potty >= jelli_potty_rules.urge_threshold;
}

void jelli_potty_accident(JelliPet *pet)
{
    pet->potty = 0u;
    pet->digesting = 0u;
    pet->behavior_flags |= JELLI_PET_FLAG_MESS;
    pet->needs[JELLI_HYGIENE] = drop(pet->needs[JELLI_HYGIENE], jelli_potty_rules.accident_hygiene);
}

void jelli_potty_clean(JelliPet *pet) { pet->behavior_flags &= (uint8_t)~JELLI_PET_FLAG_MESS; }

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

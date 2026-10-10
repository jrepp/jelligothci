#ifndef JELLI_POTTY_H
#define JELLI_POTTY_H
#include "jelli/game.h"

/* Generated from content/potty.json by cmake/JelliPotty.cmake (RFC-005). Meals and
 * drinks fill a digestion pool that becomes potty urge, a little each minute. */
typedef struct {
    uint16_t per_meal, per_drink, drain_per_minute, urge_threshold, minimum_to_go;
    uint16_t hygiene_gain, bond_gain;
} JelliPottyRules;
extern const JelliPottyRules jelli_potty_rules;

void jelli_potty_eat(JelliPet *pet);
void jelli_potty_drink(JelliPet *pet);
/* Moves digestion into urge at each whole simulated minute crossed by
 * [old_ticks, pet->ticks). True when the urge reaches its threshold. */
bool jelli_potty_advance(JelliPet *pet, uint64_t old_ticks);
/* Potty-break effect: FULL below minimum_to_go; otherwise clears the urge. */
JelliResult jelli_potty_break(JelliPet *pet);
#endif

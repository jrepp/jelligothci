#ifndef JELLI_POTTY_H
#define JELLI_POTTY_H
#include "jelli/game.h"

/* Generated from content/potty.json by cmake/JelliPotty.cmake (RFC-005). Meals and
 * drinks fill a digestion pool that becomes potty urge, a little each minute. */
typedef struct {
    uint16_t per_meal, per_drink, drain_per_minute, urge_threshold, minimum_to_go;
    uint16_t hygiene_gain, bond_gain;
    uint16_t accident_hygiene, mess_hygiene_per_minute, mess_frame_ms;
    uint32_t mess_sprites[4]; /* Asset IDs animated while a mess waits for CLEAN. */
    uint8_t mess_sprite_count;
} JelliPottyRules;
extern const JelliPottyRules jelli_potty_rules;

void jelli_potty_eat(JelliPet *pet);
void jelli_potty_drink(JelliPet *pet);
/* Moves digestion into urge at each whole simulated minute crossed by
 * [old_ticks, pet->ticks). True when the urge reaches its threshold. */
bool jelli_potty_advance(JelliPet *pet, uint64_t old_ticks);
/* An ignored potty request: clears the cycle and leaves a mess (JELLI_PET_FLAG_MESS) that
 * drains hygiene each minute until a clean-up finishes. */
void jelli_potty_accident(JelliPet *pet);
void jelli_potty_clean(JelliPet *pet);
/* Potty-break effect: FULL below minimum_to_go; otherwise clears the urge. */
JelliResult jelli_potty_break(JelliPet *pet);
#endif

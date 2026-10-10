#ifndef JELLI_NUTRITION_H
#define JELLI_NUTRITION_H
#include "jelli/game.h"
typedef struct {
    const char *name;
    uint16_t fullness, hydration;
    uint32_t icon;
} JelliFood;
extern const JelliFood jelli_foods[9];
extern const unsigned jelli_food_count;

typedef struct {
    uint16_t duration_ticks, fullness_cost, hydration_cost, energy_cost, amusement_gain;
} JelliExercise;
extern const JelliExercise jelli_exercise;
JelliResult jelli_start_exercise(JelliPet *pet);

/* Hydration uses 0..1000 and a fractional remainder in quarter-minute units. */
void jelli_hydration_advance(JelliPet *pet, uint64_t ticks);
void jelli_hydration_add(JelliPet *pet, unsigned amount);
JelliResult jelli_drink_water(JelliPet *pet);
JelliResult jelli_refill_food(JelliGame *game);
#endif

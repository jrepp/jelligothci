#ifndef JELLI_WAKE_H
#define JELLI_WAKE_H
#include "jelli/game.h"

typedef struct {
    uint32_t nap_ticks, sleep_ticks, bond_gain;
    uint8_t reaction_ticks, surprise_ticks; /* Wake reaction length; surprise opens it. */
} JelliWakeRules;
extern const JelliWakeRules jelli_wake_rules;
enum { JELLI_WAKE_NONE, JELLI_WAKE_GROGGY, JELLI_WAKE_HAPPY };
/* Accumulate only admitted sleeping time, bounded by the longest threshold. */
void jelli_wake_advance(JelliPet *pet, uint64_t ticks);
/* Call once at a real wake transition, before clearing the sleep flags. */
void jelli_wake_react(JelliPet *pet);
#endif

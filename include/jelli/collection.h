#ifndef JELLI_COLLECTION_H
#define JELLI_COLLECTION_H
#include "jelli/game.h"

/* Generated from content/pets.json by cmake/JelliCollection.cmake. */
#define JELLI_FORM_CAPACITY 8u /* Form IDs index the 8-bit reached-forms mask. */
#define JELLI_SET_FORM_CAPACITY 2u

typedef struct {
    uint8_t id, unlock_prize, evolution_set;
    const char *name, *hint;
} JelliCollectionEntry;
typedef struct {
    uint32_t portrait;
    const char *name;
} JelliCollectionForm;
/* forms[0] is the starting form; a second form grows after growth_ticks. */
typedef struct {
    uint8_t form_count;
    uint8_t forms[JELLI_SET_FORM_CAPACITY];
    uint32_t growth_ticks;
} JelliEvolutionSet;
extern const JelliCollectionForm jelli_collection_forms[];
extern const unsigned jelli_collection_form_count;
extern const JelliEvolutionSet jelli_evolution_sets[];
extern const unsigned jelli_evolution_set_count;
extern const JelliCollectionEntry jelli_collection_entries[9];
/* content/pets.json "newborn" and "start": every new pet's stats and the new game's items. */
typedef struct {
    uint16_t needs[JELLI_NEED_COUNT];
    uint16_t bond, hydration;
    uint8_t bedtime;
    uint32_t sleep_duration, phase_offset; /* Ticks. */
} JelliNewborn;
typedef struct {
    uint16_t food, gifts;
} JelliStartingItems;
extern const JelliNewborn jelli_collection_newborn;
extern const JelliStartingItems jelli_collection_start;
/* A new pet for a collection entry, in its set's starting form. */
JelliPet jelli_collection_new_pet(uint32_t id, unsigned entry);
/* Entry IDs are stable 1..9, independent of instance IDs and storage order. */
int jelli_collection_find(const JelliGame *game, unsigned entry);
/* NULL for entries outside 1..9. */
const JelliEvolutionSet *jelli_collection_set(unsigned entry);
unsigned jelli_evolution_mask(const JelliEvolutionSet *set);
/* Form ID at a set position; positions past the set fall back to its start. */
unsigned jelli_evolution_form(const JelliEvolutionSet *set, unsigned index);
/* Position of form within set, or -1 when the set does not contain it. */
int jelli_evolution_index(const JelliEvolutionSet *set, unsigned form);
/* True when a starting-form pet has aged into its set's second form. */
bool jelli_collection_growth_due(const JelliPet *pet);
void jelli_collection_unlock(JelliGame *game);
void jelli_collection_migrate(JelliGame *game);
void jelli_collection_merge_starters(JelliGame *game);
/* Map each pet onto its entry's evolution set after catalog changes. */
void jelli_collection_normalize(JelliGame *game);
/* index is a position in the pet's evolution set (0 = starting form). */
JelliResult jelli_collection_set_form(JelliGame *game, uint32_t id, uint32_t index);
bool jelli_collection_valid(const JelliGame *game);
#endif

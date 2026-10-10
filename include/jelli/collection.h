#ifndef JELLI_COLLECTION_H
#define JELLI_COLLECTION_H
#include "jelli/game.h"

typedef struct {
    uint8_t id, unlock_prize, evolution_set;
    const char *name, *hint;
} JelliCollectionEntry;
typedef struct {
    uint32_t portrait;
    const char *name;
} JelliCollectionForm;
extern const JelliCollectionForm jelli_collection_forms[2];
extern const JelliCollectionEntry jelli_collection_entries[9];
extern const uint32_t jelli_collection_growth_ticks;
/* Entry IDs are stable 1..9, independent of instance IDs and storage order. */
int jelli_collection_find(const JelliGame *game, unsigned entry);
void jelli_collection_unlock(JelliGame *game);
void jelli_collection_migrate(JelliGame *game);
void jelli_collection_merge_starters(JelliGame *game);
JelliResult jelli_collection_set_form(JelliGame *game, uint32_t id, uint32_t form);
bool jelli_collection_valid(const JelliGame *game);
#endif

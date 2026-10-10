#ifndef JELLI_PET_COLLECTION_H
#define JELLI_PET_COLLECTION_H
#include "pet_canvas.h"
#include "jelli/collection.h"
bool jelli_pet_collection_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button);
JelliResult jelli_pet_collection_available(JelliPetUi *ui, const JelliGame *game, unsigned slot);
void jelli_pet_collection_select(JelliPetUi *ui, JelliGame *game, unsigned slot);
void jelli_pet_collection_key(const JelliPetUi *ui, const JelliGame *game, JelliPetRenderKey *key);
bool jelli_pet_collection_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b);
void jelli_pet_collection_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game);
#endif

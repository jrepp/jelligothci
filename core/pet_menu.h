#ifndef JELLI_PET_MENU_H
#define JELLI_PET_MENU_H
#include "pet_canvas.h"
#include "jelli/pet_ui.h"
void jelli_pet_discovery_draw(Canvas *c, const JelliPetUi *ui, const JelliPetRenderKey *v);
void jelli_pet_menu_ring(Canvas *c, const JelliGame *game, const JelliPetUi *ui);
const char *jelli_pet_menu_title(const JelliPetUi *ui);
const char *jelli_pet_menu_hint(const JelliPetUi *ui, const JelliGame *game);
#endif

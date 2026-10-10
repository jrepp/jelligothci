#ifndef JELLI_PET_FOOD_H
#define JELLI_PET_FOOD_H
#include "pet_canvas.h"
bool jelli_pet_food_button(unsigned slot, JelliPetUiButton *button);
JelliResult jelli_pet_food_available(JelliPetUi *ui, const JelliGame *game, unsigned slot);
void jelli_pet_food_select(JelliPetUi *ui, JelliGame *game, unsigned slot);
void jelli_pet_food_draw(Canvas *c, const JelliPetUi *ui);
#endif

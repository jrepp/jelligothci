#ifndef JELLI_PET_COLLECTIBLE_UI_H
#define JELLI_PET_COLLECTIBLE_UI_H
#include "pet_canvas.h"

bool jelli_pet_collectible_page(unsigned page);
bool jelli_pet_collectible_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button);
bool jelli_pet_collectible_tap(JelliPetUi *ui, JelliGame *game, int x, int y);
bool jelli_pet_collectible_swipe(JelliPetUi *ui, JelliGame *game, int dx, int dy);
void jelli_pet_collectible_back(JelliPetUi *ui);
void jelli_pet_collectible_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game);
bool jelli_pet_collectible_win_update(JelliPetUi *ui, JelliGame *game, uint64_t now);
void jelli_pet_collectible_win_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game);
void jelli_pet_collectible_session_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game);
void jelli_pet_collectible_key(const JelliPetUi *ui, const JelliGame *game, JelliPetRenderKey *key);
bool jelli_pet_collectible_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b);
unsigned jelli_pet_collectible_count(unsigned category);
unsigned jelli_pet_collectible_prize(unsigned category, unsigned position);
unsigned jelli_pet_collectible_recipe(unsigned prize);
bool jelli_pet_bubble_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button);
void jelli_pet_bubble_action(JelliPetUi *ui, JelliGame *game, unsigned slot);
void jelli_pet_bubble_draw(Canvas *canvas, const JelliCollectibleSession *session);
void jelli_pet_bubble_checkpoint(JelliPetUi *ui, const JelliCollectibleSession *before,
                                 const JelliCollectibleSession *after);
void jelli_pet_cartridge_draw(Canvas *canvas, const JelliGame *game, unsigned prize);
#endif

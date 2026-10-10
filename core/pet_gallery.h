#ifndef JELLI_PET_GALLERY_H
#define JELLI_PET_GALLERY_H

#include "pet_canvas.h"

bool jelli_pet_gallery_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button);
JelliResult jelli_pet_gallery_available(const JelliPetUi *ui, const JelliGame *game, unsigned slot);
void jelli_pet_gallery_select(JelliPetUi *ui, const JelliGame *game, unsigned slot);
bool jelli_pet_gallery_tap(JelliPetUi *ui, JelliGame *game, int x, int y);
void jelli_pet_gallery_update(JelliPetUi *ui, const JelliGame *game, uint64_t time_ms);
void jelli_pet_gallery_key(const JelliPetUi *ui, const JelliGame *game, uint64_t time_ms,
                           JelliPetRenderKey *key);
bool jelli_pet_gallery_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b);
void jelli_pet_gallery_draw(Canvas *canvas, const JelliPetRenderKey *view);
void jelli_pet_gallery_action(JelliPetUi *ui, JelliGame *game, unsigned slot);
void jelli_pet_gallery_draw_action(Canvas *c, const JelliPetUi *ui, const JelliGame *game);
void jelli_pet_gallery_draw_latched(Canvas *canvas, const JelliPetRenderKey *view);

#endif

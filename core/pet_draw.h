#ifndef JELLI_PET_DRAW_H
#define JELLI_PET_DRAW_H
#include "jelli/pet_ui.h"
bool jelli_pet_touch_actor(JelliPetUi *ui, JelliGame *game, int x, int y);
void jelli_pet_actor_layout(JelliPetUi *ui, const JelliPetRenderKey *view);
uint16_t jelli_pet_background(uint8_t location, unsigned x, unsigned y);
uint16_t jelli_pet_night_color(uint16_t color, uint8_t night);
void jelli_pet_atmosphere(JelliPetUi *ui, const JelliPet *pet, uint64_t time,
                          JelliPetRenderKey *view);
void jelli_pet_bubbles(JelliPetUi *ui, bool asleep, uint64_t time);
void jelli_pet_sleep_particles(JelliPetUi *ui, bool asleep, uint64_t time);
void jelli_pet_draw_background(JelliSurface *surface, const JelliPetRenderKey *view,
                               JelliRect region);
void jelli_pet_draw(JelliSurface *surface, const JelliGame *game, const JelliPetUi *ui,
                    const JelliPetRenderKey *view);
void jelli_pet_draw_region(JelliSurface *surface, const JelliGame *game, const JelliPetUi *ui,
                           const JelliPetRenderKey *view, JelliRect region);
void jelli_pet_draw_particles(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui);
void jelli_pet_draw_tile(JelliSurface *surface, const JelliPetRenderKey *view);
void jelli_pet_timing(JelliPetUi *ui, const JelliPet *pet, uint64_t time, JelliPetRenderKey *view);
#endif

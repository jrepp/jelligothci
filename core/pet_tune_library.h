#ifndef JELLI_PET_TUNE_LIBRARY_H
#define JELLI_PET_TUNE_LIBRARY_H
#include "pet_canvas.h"
#include "jelli/music.h"
#define JELLI_TUNE_DURATION_MS (JELLI_MUSIC_SAMPLES / JELLI_MUSIC_RATE * 1000u)
unsigned jelli_pet_library_count(uint16_t seen);
unsigned jelli_pet_library_song(uint16_t seen, unsigned position);
bool jelli_pet_library_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button);
bool jelli_pet_tune_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button);
void jelli_pet_library_draw(Canvas *canvas, const JelliPetUi *ui, const JelliGame *game);
void jelli_pet_tune_start(JelliPetUi *ui, const JelliGame *game, unsigned song);
void jelli_pet_tune_action(JelliPetUi *ui, unsigned slot);
void jelli_pet_tune_update(JelliPetUi *ui, const JelliGame *game, uint64_t now);
void jelli_pet_tune_draw(Canvas *canvas, const JelliPetUi *ui, const JelliGame *game);
#endif

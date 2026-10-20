#ifndef JELLI_PET_EFFECT_H
#define JELLI_PET_EFFECT_H
#include "jelli/pet_ui.h"
/* Explicit cosmetic playback, never mutates game or save state. A successful
 * start replaces playback; an unknown key leaves it unchanged. Time uses the
 * engine animation clock (pauses with it). Definitions are embedded/immutable.
 * Opening a menu, changing pets/forms or a backward clock cancels playback. */
bool jelli_pet_effect_start(JelliPetUi *ui, const char *key, uint64_t animation_ms);
void jelli_pet_effect_stop(JelliPetUi *ui);
void jelli_pet_effect_prepare(JelliPetUi *ui, JelliPetRenderKey *view, uint64_t time);
#endif

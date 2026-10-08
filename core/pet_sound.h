#ifndef JELLI_PET_SOUND_H
#define JELLI_PET_SOUND_H

#include "pet_canvas.h"

/* Settings center panel while the sound editor is open: the level as a large
 * number, "SOUND", and a ten-segment bar. Filled segments are solid and empty
 * ones hollow outlines, so the level never depends on color alone. */
void jelli_pet_sound_draw(Canvas *canvas, const JelliPetRenderKey *view);

#endif

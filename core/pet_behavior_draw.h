#ifndef JELLI_PET_BEHAVIOR_DRAW_H
#define JELLI_PET_BEHAVIOR_DRAW_H
#include "pet_canvas.h"

/* Behaviour state effect and prop, plus a potty mess, from content data (RFC-005). */
void jelli_pet_draw_behavior(Canvas *c, const JelliPetUi *ui, const JelliPetRenderKey *v);
/* Caption for the current behaviour state or mess; "" when neither applies. */
const char *jelli_pet_behavior_caption(const JelliPetRenderKey *v);
#endif

#ifndef JELLI_PET_BEHAVIOR_DRAW_H
#define JELLI_PET_BEHAVIOR_DRAW_H
#include "pet_canvas.h"

/* Behaviour state effect and prop, plus a potty mess, from content data (RFC-005). */
void jelli_pet_draw_behavior(Canvas *c, const JelliPetUi *ui, const JelliPetRenderKey *v);
/* Screen rectangle of a mess sprite frame (1-based), beside the pet; false when hidden. */
bool jelli_pet_mess_bounds(const JelliPetUi *ui, unsigned mess, JelliRect *bounds);
/* Tap on a visible mess: starts the clean-up. True when the tap landed on it. */
bool jelli_pet_tap_mess(JelliPetUi *ui, JelliGame *game, int x, int y);
/* Caption for the current behaviour state or mess; "" when neither applies. */
const char *jelli_pet_behavior_caption(const JelliPetRenderKey *v);
#endif

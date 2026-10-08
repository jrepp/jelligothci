#ifndef JELLI_PET_BATTERY_H
#define JELLI_PET_BATTERY_H

#include "pet_canvas.h"

/* Fills the battery fields from the persisted setting and the effective reading.
 * The reading is recorded only while the home indicator is visible, so a hidden
 * indicator or an unchanged canonical reading never causes a redraw. */
void jelli_pet_battery_key(const JelliPetUi *ui, const JelliGame *game, JelliPetRenderKey *key);
bool jelli_pet_battery_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b);
/* Copies only the battery fields, for detecting an indicator-only change. */
void jelli_pet_battery_copy(JelliPetRenderKey *destination, const JelliPetRenderKey *source);
/* Fixed screen rectangle containing every possible indicator icon and caption. */
JelliRect jelli_pet_battery_bounds(void);
void jelli_pet_battery_draw(Canvas *canvas, const JelliPetRenderKey *view);

#endif

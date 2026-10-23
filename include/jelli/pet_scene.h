#ifndef JELLI_PET_SCENE_H
#define JELLI_PET_SCENE_H

#include "jelli/pet_ui.h"

/* Opt-in presentation override, independent of saved location/gameplay. Caller
 * supplies persistent cache storage; failure leaves the old selection unchanged.
 * Definition keys come from content/scenes.json. Clear restores flat locations. */
bool jelli_pet_scene_select(JelliPetUi *ui, JelliScene *scene, const char *key);
void jelli_pet_scene_clear(JelliPetUi *ui);
/* Called once before render-key construction. Resolves all current asset views,
 * retaining the complete cached image if a view fails validation. No IO or heap.
 * Hosts replacing pixels in-place must set ui->rendered=false, as live reload does. */
void jelli_pet_scene_prepare(JelliPetUi *ui);

/* Automatic Activities-menu backdrop. An explicit caller-owned override wins.
 * Uses presentation time only; never changes location, rewards or saved state. */
void jelli_pet_activity_scene(JelliPetUi *ui, const JelliGame *game, uint64_t time, bool paused);

#endif

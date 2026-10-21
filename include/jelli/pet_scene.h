#ifndef JELLI_PET_SCENE_H
#define JELLI_PET_SCENE_H

#include "jelli/pet_ui.h"

/* Opt-in presentation override, independent of saved location/gameplay. Caller
 * supplies persistent cache storage; failure leaves the old selection unchanged.
 * Definition keys come from content/scenes.json. Clear returns to automatic
 * activity selection when bound, or the flat location otherwise. */
bool jelli_pet_scene_select(JelliPetUi *ui, JelliScene *scene, const char *key);
void jelli_pet_scene_clear(JelliPetUi *ui);
/* Called once before render-key construction. Resolves all current asset views,
 * retaining the complete cached image if a view fails validation. No IO or heap.
 * Hosts replacing pixels in-place must set ui->rendered=false, as live reload does. */
void jelli_pet_scene_prepare(JelliPetUi *ui);
/* Opt in to authored activity bindings; explicit scene selection takes priority. */
void jelli_pet_scene_bind_activities(JelliPetUi *ui, JelliScene *cache);
void jelli_pet_scene_update(JelliPetUi *ui, const JelliPet *pet, uint64_t animation_ms);

#endif

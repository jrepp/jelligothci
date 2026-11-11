#include "jelli/pet_scene.h"
#include "jelli/activities.h"
#include <string.h>

static const JelliSceneDefinition *activity_definition(const JelliPet *pet)
{
    const char *name = NULL;
    if (pet->moment && pet->moment <= jelli_moment_count)
        name = jelli_moments[pet->moment - 1u].name;
    else if (pet->activity == JELLI_EXERCISING)
        name = "WORKOUT";
    if (!name || pet->activity == JELLI_IDLE)
        return NULL;
    for (unsigned i = 0; i < jelli_scene_definition_count; ++i) {
        const JelliSceneDefinition *definition = &jelli_scene_definitions[i];
        if (definition->activity && strcmp(definition->activity, name) == 0)
            return definition;
    }
    return NULL;
}

void jelli_pet_activity_scene(JelliPetUi *ui, const JelliGame *game, uint64_t time, bool paused)
{
    if (!ui || !game || game->active >= game->count)
        return;
    uint64_t elapsed = time >= ui->activity_scene_ms ? time - ui->activity_scene_ms : 0u;
    ui->activity_scene_ms = time;
    /* Explicit scene previews retain their caller's clock and motion inputs. */
    if (ui->scene && ui->scene != &ui->activity_scene)
        return;
    const JelliPet *pet = &game->pets[game->active];
    const JelliSceneDefinition *definition = game->resuming ? NULL : activity_definition(pet);
    if (!definition) {
        jelli_pet_scene_clear(ui);
        ui->activity_scene_remainder = 0;
        return;
    }
    if (!ui->scene || ui->activity_scene.definition != definition ||
        ui->activity_scene_pet != pet->id || ui->activity_scene_due != pet->interaction_due) {
        /* A different activity must not inherit the previous activity's scenery.
         * Missing live assets fall back to the location and are retried next frame. */
        jelli_pet_scene_clear(ui);
        ui->activity_scene_remainder = 0;
        if (!jelli_pet_scene_select(ui, &ui->activity_scene, definition->key))
            return;
        ui->activity_scene_pet = pet->id;
        ui->activity_scene_due = pet->interaction_due;
        elapsed = 0u;
    }
    if (paused) {
        ui->activity_scene_remainder = 0;
        return;
    }
    /* Drop stall backlog; bounded Q8 arithmetic preserves subpixel progress. */
    if (elapsed > 250u)
        elapsed = 250u;
    int32_t distance =
        (int32_t)elapsed * definition->speed * JELLI_SCENE_TILT_ONE + ui->activity_scene_remainder;
    ui->activity_scene_remainder = distance % 1000;
    jelli_scene_advance(&ui->activity_scene, 0, 0, distance / 1000);
}

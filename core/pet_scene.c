#include "jelli/pet_scene.h"
#include <string.h>

static bool resolve(const JelliPetUi *ui, const JelliSceneDefinition *definition,
                    JelliSceneImage images[JELLI_SCENE_LAYER_CAPACITY])
{
    for (unsigned i = 0; i < definition->count; ++i) {
        const JelliAsset *asset = jelli_asset_lookup(ui->assets, definition->layers[i].asset);
        if (!asset || asset->width != definition->layers[i].width ||
            asset->height != definition->layers[i].height)
            return false;
        images[i] = (JelliSceneImage){.pixels = asset->pixels,
                                      .pixel_count = (size_t)asset->width * asset->height,
                                      .mask = asset->mask,
                                      .mask_bytes = (size_t)asset->mask_stride * asset->height,
                                      .width = asset->width,
                                      .height = asset->height,
                                      .mask_stride = asset->mask_stride};
    }
    return jelli_scene_images_valid(definition, images, definition->count);
}

bool jelli_pet_scene_select(JelliPetUi *ui, JelliScene *scene, const char *key)
{
    if (!ui || !scene || !key)
        return false;
    const JelliSceneDefinition *definition = NULL;
    for (unsigned i = 0; i < jelli_scene_definition_count; ++i)
        if (!strcmp(jelli_scene_definitions[i].key, key)) {
            definition = &jelli_scene_definitions[i];
            break;
        }
    if (!definition)
        return false;
    JelliSceneImage images[JELLI_SCENE_LAYER_CAPACITY] = {0};
    if (!resolve(ui, definition, images))
        return false;
    /* All fallible checks precede publication; definitions and views stay stable
     * on this single calling thread through initialization/composition. */
    if (!jelli_scene_init(scene, definition) ||
        !jelli_scene_compose(scene, images, definition->count, true))
        return false;
    ui->scene = scene;
    ui->scene_automatic = false;
    ui->scene_clock_started = false;
    ui->scene_assets = ui->assets;
    ui->rendered = false;
    return true;
}

void jelli_pet_scene_clear(JelliPetUi *ui)
{
    if (ui && ui->scene) {
        ui->scene = NULL;
        ui->scene_automatic = false;
        ui->scene_clock_started = false;
        ui->scene_assets = NULL;
        ui->rendered = false;
    }
}

void jelli_pet_scene_prepare(JelliPetUi *ui)
{
    if (!ui || !ui->scene || !ui->scene->definition)
        return;
    JelliSceneImage images[JELLI_SCENE_LAYER_CAPACITY] = {0};
    const JelliSceneDefinition *definition = ui->scene->definition;
    if (resolve(ui, definition, images) &&
        jelli_scene_compose(ui->scene, images, definition->count,
                            !ui->rendered || ui->assets != ui->scene_assets))
        ui->scene_assets = ui->assets;
}

void jelli_pet_scene_bind_activities(JelliPetUi *ui, JelliScene *cache)
{
    if (!ui)
        return;
    if (ui->scene_automatic)
        jelli_pet_scene_clear(ui);
    ui->activity_scene = cache;
}

static void activity_scene(JelliPetUi *ui, const JelliPet *pet)
{
    if (!ui->activity_scene || (ui->scene && !ui->scene_automatic))
        return;
    const char *key = NULL;
    if (pet->moment)
        for (unsigned i = 0; i < jelli_scene_definition_count; ++i)
            if (jelli_scene_definitions[i].activity == pet->moment) {
                key = jelli_scene_definitions[i].key;
                break;
            }
    if (!key) {
        if (ui->scene_automatic)
            jelli_pet_scene_clear(ui);
    } else if (!ui->scene || strcmp(ui->scene->definition->key, key) != 0) {
        if (jelli_pet_scene_select(ui, ui->activity_scene, key))
            ui->scene_automatic = true;
    }
}

void jelli_pet_scene_update(JelliPetUi *ui, const JelliPet *pet, uint64_t animation_ms)
{
    if (!ui || !pet)
        return;
    activity_scene(ui, pet);
    if (!ui->scene)
        return;
    if (!ui->scene_clock_started || animation_ms < ui->scene_anchor_ms) {
        ui->scene_anchor_ms = animation_ms;
        ui->scene_clock_started = true;
        ui->scene_elapsed_ms = 0;
    }
    uint64_t elapsed = animation_ms - ui->scene_anchor_ms;
    ui->scene_anchor_ms = animation_ms;
    /* Drop stall backlog; unsigned elapsed accumulation wraps harmlessly after centuries. */
    ui->scene_elapsed_ms += elapsed > 250u ? 250u : elapsed;
    jelli_scene_animate(ui->scene, ui->scene_elapsed_ms);
}

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
    ui->scene_assets = ui->assets;
    ui->rendered = false;
    return true;
}

void jelli_pet_scene_clear(JelliPetUi *ui)
{
    if (ui && ui->scene) {
        ui->scene = NULL;
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

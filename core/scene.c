#include "jelli/scene.h"
#include <string.h>

enum { PHASE_ONE = 25600 };
_Static_assert(sizeof(JelliScene) <= 9216u, "Scene cache exceeds 9 KiB budget");

static bool valid_definition(const JelliSceneDefinition *definition)
{
    if (!definition || !definition->count || definition->count > JELLI_SCENE_LAYER_CAPACITY)
        return false;
    for (unsigned i = 0; i < definition->count; ++i) {
        const JelliSceneLayer *layer = &definition->layers[i];
        if (!layer->width || layer->width > JELLI_SCENE_IMAGE_LIMIT || !layer->height ||
            layer->height > JELLI_SCENE_IMAGE_LIMIT || layer->x < -256 || layer->x > 256 ||
            layer->y < -256 || layer->y > 256 || layer->tilt_x > 16u || layer->tilt_y > 16u ||
            layer->travel > 100u || (layer->travel && !layer->repeat_x))
            return false;
    }
    return true;
}

static int32_t modulo(int32_t value, int32_t period)
{
    int32_t result = value % period;
    return result < 0 ? result + period : result;
}

/* floor(value/PHASE_ONE + 0.5), including negative ties (browser Math.round). */
static int16_t rounded(int32_t value)
{
    int32_t shifted = value + PHASE_ONE / 2;
    int32_t whole = shifted / PHASE_ONE;
    if (shifted < 0 && shifted % PHASE_ONE)
        --whole;
    return (int16_t)whole; /* Validated coordinates and phase bound this to +/- 528. */
}

static int32_t clamp(int32_t value, int32_t limit)
{
    return value < -limit ? -limit : value > limit ? limit : value;
}

bool jelli_scene_init(JelliScene *scene, const JelliSceneDefinition *definition)
{
    if (!scene || !valid_definition(definition))
        return false;
    memset(scene, 0, sizeof(*scene));
    scene->definition = definition;
    scene->dirty = true;
    jelli_scene_advance(scene, 0, 0, 0);
    return true;
}

void jelli_scene_advance(JelliScene *scene, int16_t tilt_x, int16_t tilt_y, int32_t travel_delta_q8)
{
    if (!scene || !scene->definition)
        return;
    int32_t tx = clamp(tilt_x, JELLI_SCENE_TILT_ONE);
    int32_t ty = clamp(tilt_y, JELLI_SCENE_TILT_ONE);
    int32_t delta = clamp(travel_delta_q8, JELLI_SCENE_DELTA_LIMIT);
    for (unsigned i = 0; i < scene->definition->count; ++i) {
        const JelliSceneLayer *layer = &scene->definition->layers[i];
        int32_t period = (int32_t)layer->width * PHASE_ONE;
        /* Max intermediate magnitude < 14 million after input saturation. */
        scene->phase[i] = modulo(scene->phase[i] + delta * layer->travel, period);
        int16_t x =
            rounded((int32_t)layer->x * PHASE_ONE - tx * layer->tilt_x * 100 - scene->phase[i]);
        int16_t y = rounded((int32_t)layer->y * PHASE_ONE - ty * layer->tilt_y * 100);
        if (layer->repeat_x)
            x = (int16_t)modulo(x, layer->width);
        if (layer->visible && (x != scene->x[i] || y != scene->y[i]))
            scene->dirty = true;
        scene->x[i] = x;
        scene->y[i] = y;
    }
}

bool jelli_scene_images_valid(const JelliSceneDefinition *definition, const JelliSceneImage *images,
                              size_t count)
{
    if (!valid_definition(definition) || !images || count != definition->count)
        return false;
    for (unsigned i = 0; i < definition->count; ++i) {
        const JelliSceneImage *image = &images[i];
        const JelliSceneLayer *layer = &definition->layers[i];
        if (image->width != layer->width || image->height != layer->height ||
            image->mask_stride < (image->width + 7u) / 8u || !image->pixels || !image->mask ||
            image->pixel_count < (size_t)image->width * image->height ||
            image->mask_bytes < (size_t)image->mask_stride * image->height)
            return false;
    }
    return true;
}

static void composite_layer(JelliScene *scene, unsigned index, const JelliSceneImage *image)
{
    const JelliSceneLayer *layer = &scene->definition->layers[index];
    for (unsigned y = 0; y < JELLI_SCENE_SIZE; ++y) {
        int row = (int)y - scene->y[index];
        if (row < 0 || row >= image->height)
            continue;
        for (unsigned x = 0; x < JELLI_SCENE_SIZE; ++x) {
            int column = (int)x - scene->x[index];
            if (layer->repeat_x)
                column = (int)modulo(column, image->width);
            if (column < 0 || column >= image->width)
                continue;
            size_t mask_index = (size_t)row * image->mask_stride + (unsigned)column / 8u;
            unsigned bit = 7u - (unsigned)column % 8u;
            if (image->mask[mask_index] & (1u << bit))
                scene->pixels[y * JELLI_SCENE_SIZE + x] =
                    image->pixels[(size_t)row * image->width + (unsigned)column];
        }
    }
}

bool jelli_scene_compose(JelliScene *scene, const JelliSceneImage *images, size_t count,
                         bool invalidate)
{
    if (!scene || !jelli_scene_images_valid(scene->definition, images, count))
        return false;
    if (scene->ready && !scene->dirty && !invalidate)
        return true;
    for (unsigned i = 0; i < JELLI_SCENE_SIZE * JELLI_SCENE_SIZE; ++i)
        scene->pixels[i] = scene->definition->fill;
    for (unsigned i = 0; i < scene->definition->count; ++i)
        if (scene->definition->layers[i].visible)
            composite_layer(scene, i, &images[i]);
    ++scene->revision;
    scene->ready = true;
    scene->dirty = false;
    return true;
}

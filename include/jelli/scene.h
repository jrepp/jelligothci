#ifndef JELLI_SCENE_H
#define JELLI_SCENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JELLI_SCENE_SIZE 64u
#define JELLI_SCENE_LAYER_CAPACITY 12u
#define JELLI_SCENE_IMAGE_LIMIT 256u
#define JELLI_SCENE_TILT_ONE 256
#define JELLI_SCENE_DELTA_LIMIT 65536 /* Q8 source pixels per update, signed. */

typedef struct {
    uint32_t asset;
    uint16_t width, height;
    int16_t x, y;
    uint8_t tilt_x, tilt_y, travel;
    bool repeat_x, visible;
} JelliSceneLayer;

typedef struct {
    const char *key;
    uint16_t fill;
    uint8_t count;
    JelliSceneLayer layers[JELLI_SCENE_LAYER_CAPACITY];
} JelliSceneDefinition;

/* Borrowed native-endian RGB565 and MSB-first binary coverage. Capacities count
 * elements/bytes respectively. Buffers need only outlive compose(). No aliasing
 * with its output state. Both buffers are required, including opaque images. */
typedef struct {
    const uint16_t *pixels;
    size_t pixel_count;
    const uint8_t *mask;
    size_t mask_bytes;
    uint16_t width, height, mask_stride;
} JelliSceneImage;

/* Caller-owned persistent storage; never put this cache on a small task stack.
 * Definition is immutable and outlives the initialized state. No image pointers
 * are retained. Phases use 1/25600px units to preserve integer travel percentages.
 * Public fields are read-only to callers; use init/advance/compose to mutate. */
typedef struct {
    const JelliSceneDefinition *definition;
    int32_t phase[JELLI_SCENE_LAYER_CAPACITY];
    int16_t x[JELLI_SCENE_LAYER_CAPACITY], y[JELLI_SCENE_LAYER_CAPACITY];
    uint16_t pixels[JELLI_SCENE_SIZE * JELLI_SCENE_SIZE];
    uint32_t revision;
    bool dirty, ready;
} JelliScene;

/* Invalid initialization leaves state unchanged. Call with distinct, nonaliasing
 * state/definition. init clears the cache; resetting a selected scene also needs
 * its renderer's invalidation (jelli_pet_scene_select does this). */
bool jelli_scene_init(JelliScene *scene, const JelliSceneDefinition *definition);
/* Saturates normalized Q8 tilt and signed Q8 travel delta. Positive travel moves
 * art left. Zero elapsed travel remains zero; callers own clocks and pacing. */
void jelli_scene_advance(JelliScene *scene, int16_t tilt_x, int16_t tilt_y,
                         int32_t travel_delta_q8);
bool jelli_scene_images_valid(const JelliSceneDefinition *definition, const JelliSceneImage *images,
                              size_t count);
/* Validates the complete set before writing. Failure preserves the previous
 * cache/revision and leaves pending motion dirty for retry. invalidate requests
 * recomposition after source pixel changes even when offsets are unchanged.
 * A successful changed composition increments revision (unsigned wrap). */
bool jelli_scene_compose(JelliScene *scene, const JelliSceneImage *images, size_t count,
                         bool invalidate);

/* Generated for rendered builds only; pure core users supply their own tables. */
extern const JelliSceneDefinition jelli_scene_definitions[];
extern const unsigned jelli_scene_definition_count;

#endif

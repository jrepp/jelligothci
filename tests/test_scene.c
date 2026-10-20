#include "jelli/scene.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static JelliScene scene, saved, other;
static const uint16_t tile[] = {10, 20, 30, 40}, overlay[] = {50};
static const uint8_t mask[] = {0xc0, 0x80}, opaque[] = {0x80};
static const JelliSceneDefinition definition = {
    .key = "test",
    .fill = 77,
    .count = 2,
    .layers = {{.width = 2,
                .height = 2,
                .tilt_x = 1,
                .tilt_y = 1,
                .travel = 100,
                .repeat_x = true,
                .visible = true},
               {.width = 1, .height = 1, .x = 1, .visible = true}}};
static const JelliSceneImage images[] = {{tile, 4, mask, 2, 2, 2, 1},
                                         {overlay, 1, opaque, 1, 1, 1, 1}};

static void sampling_and_motion(void)
{
    CHECK(jelli_scene_init(&scene, &definition));
    CHECK(jelli_scene_compose(&scene, images, 2, false));
    CHECK(scene.pixels[0] == 10 && scene.pixels[1] == 50 && scene.pixels[63] == 20);
    CHECK(scene.pixels[64] == 30 && scene.pixels[65] == 77 && scene.pixels[4095] == 77);
    uint32_t revision = scene.revision;
    CHECK(jelli_scene_compose(&scene, images, 2, false));
    CHECK(scene.revision == revision);
    jelli_scene_advance(&scene, 0, 0, 256);
    CHECK(jelli_scene_compose(&scene, images, 2, false));
    CHECK(scene.pixels[0] == 20 && scene.pixels[1] == 50);
    CHECK(scene.pixels[64] == 77 && scene.pixels[65] == 30);
    jelli_scene_advance(&scene, 0, 0, -256);
    CHECK(jelli_scene_compose(&scene, images, 2, false) && scene.pixels[0] == 10);
    revision = scene.revision;
    jelli_scene_advance(&scene, 128, 0, 0); /* Negative half rounds toward zero. */
    CHECK(jelli_scene_compose(&scene, images, 2, false) && scene.revision == revision);
    jelli_scene_advance(&scene, 129, 0, 0);
    CHECK(jelli_scene_compose(&scene, images, 2, false) && scene.pixels[0] == 20);
    jelli_scene_advance(&scene, 0, 256, 0);
    CHECK(jelli_scene_compose(&scene, images, 2, false) && scene.pixels[0] == 30);
    CHECK(scene.pixels[1] == 50 && scene.pixels[64] == 77);
}

static void bounded_arithmetic(void)
{
    JelliSceneDefinition slow = definition;
    slow.layers[0].travel = 15;
    CHECK(jelli_scene_init(&scene, &slow) && jelli_scene_init(&other, &slow));
    for (unsigned i = 0; i < 1500; ++i)
        jelli_scene_advance(&scene, 0, 0, 1);
    jelli_scene_advance(&other, 0, 0, 1500);
    CHECK(scene.phase[0] == other.phase[0] && scene.x[0] == other.x[0]);
    for (unsigned i = 0; i < 1500; ++i)
        jelli_scene_advance(&scene, 0, 0, -1);
    CHECK(scene.phase[0] == 0 && scene.x[0] == 0);
    CHECK(jelli_scene_init(&scene, &definition) && jelli_scene_init(&other, &definition));
    jelli_scene_advance(&scene, INT16_MAX, INT16_MIN, INT32_MAX);
    jelli_scene_advance(&other, 256, -256, JELLI_SCENE_DELTA_LIMIT);
    CHECK(scene.x[0] == other.x[0] && scene.y[0] == other.y[0]);
    jelli_scene_advance(&scene, INT16_MIN, INT16_MAX, INT32_MIN);
    jelli_scene_advance(&other, -256, 256, -JELLI_SCENE_DELTA_LIMIT);
    CHECK(scene.phase[0] == 0 && scene.x[0] == other.x[0] && scene.y[0] == other.y[0]);
    for (unsigned i = 0; i < 100000; ++i)
        jelli_scene_advance(&scene, 0, 0, INT32_MAX);
    CHECK(scene.phase[0] >= 0 && scene.phase[0] < 2 * 25600);
}

static void unchanged(void)
{
    CHECK(saved.definition == scene.definition && saved.revision == scene.revision);
    CHECK(saved.dirty == scene.dirty && saved.ready == scene.ready);
    CHECK(!memcmp(saved.phase, scene.phase, sizeof(scene.phase)));
    CHECK(!memcmp(saved.x, scene.x, sizeof(scene.x)));
    CHECK(!memcmp(saved.y, scene.y, sizeof(scene.y)));
    CHECK(!memcmp(saved.pixels, scene.pixels, sizeof(scene.pixels)));
}

static void failure_atomicity(void)
{
    CHECK(jelli_scene_init(&scene, &definition));
    CHECK(jelli_scene_compose(&scene, images, 2, false));
    memcpy(&saved, &scene, sizeof(scene));
    JelliSceneDefinition bad = definition;
    bad.count = JELLI_SCENE_LAYER_CAPACITY + 1;
    CHECK(!jelli_scene_init(&scene, &bad));
    unchanged();
    bad = definition;
    bad.layers[0].repeat_x = false;
    CHECK(!jelli_scene_init(&scene, &bad));
    bad = definition;
    bad.layers[0].width = JELLI_SCENE_IMAGE_LIMIT + 1;
    CHECK(!jelli_scene_init(&scene, &bad));
    CHECK(!jelli_scene_init(&scene, NULL) && !jelli_scene_init(NULL, &definition));
    JelliSceneImage short_images[2];
    memcpy(short_images, images, sizeof(images));
    short_images[1].mask_bytes = 0;
    CHECK(!jelli_scene_compose(&scene, short_images, 2, true));
    CHECK(!jelli_scene_compose(&scene, images, 1, true));
    short_images[1] = images[1];
    short_images[0].pixel_count = 3;
    CHECK(!jelli_scene_compose(&scene, short_images, 2, true));
    unchanged();
    CHECK(!jelli_scene_compose(NULL, images, 2, false));
    jelli_scene_advance(NULL, 0, 0, 0);
    CHECK(jelli_scene_compose(&scene, images, 2, true));
    CHECK(scene.revision == saved.revision + 1);
}

static void clipped_and_hidden(void)
{
    JelliSceneDefinition clipped = definition;
    clipped.layers[0].visible = false;
    clipped.layers[1].x = -256;
    CHECK(jelli_scene_init(&scene, &clipped));
    CHECK(jelli_scene_compose(&scene, images, 2, false));
    for (unsigned i = 0; i < JELLI_SCENE_SIZE * JELLI_SCENE_SIZE; ++i)
        CHECK(scene.pixels[i] == clipped.fill);
    uint32_t revision = scene.revision;
    jelli_scene_advance(&scene, 256, 256, 256);
    CHECK(jelli_scene_compose(&scene, images, 2, false) && scene.revision == revision);
}

static void maximum_capacity(void)
{
    static uint16_t pixels[256u * 256u];
    static uint8_t coverage[33u * 256u];
    JelliSceneDefinition maximum = {.key = "maximum", .count = JELLI_SCENE_LAYER_CAPACITY};
    JelliSceneImage views[JELLI_SCENE_LAYER_CAPACITY] = {0};
    memset(coverage, 0xff, sizeof(coverage));
    pixels[256u * 256u - 1u] = 12345;
    for (unsigned i = 0; i < JELLI_SCENE_LAYER_CAPACITY; ++i) {
        maximum.layers[i] =
            (JelliSceneLayer){.width = 256, .height = 256, .x = -192, .y = -192, .visible = true};
        views[i] = (JelliSceneImage){
            pixels, sizeof(pixels) / sizeof(pixels[0]), coverage, sizeof(coverage), 256, 256, 33};
    }
    CHECK(jelli_scene_init(&scene, &maximum));
    CHECK(jelli_scene_compose(&scene, views, JELLI_SCENE_LAYER_CAPACITY, true));
    CHECK(scene.pixels[JELLI_SCENE_SIZE * JELLI_SCENE_SIZE - 1u] == 12345);
    views[JELLI_SCENE_LAYER_CAPACITY - 1u].mask_bytes--;
    CHECK(!jelli_scene_compose(&scene, views, JELLI_SCENE_LAYER_CAPACITY, true));
}

int main(void)
{
    sampling_and_motion();
    bounded_arithmetic();
    failure_atomicity();
    clipped_and_hidden();
    maximum_capacity();
    printf("Scene transforms/composition passed; persistent cache: %zu bytes\n", sizeof(scene));
    return 0;
}

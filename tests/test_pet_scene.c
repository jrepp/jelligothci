#include "jelli/pet_scene.h"
#include "../core/pet_draw.h"
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

enum { STRIDE = JELLI_WIDTH + 7, GUARD = 16, COUNT = STRIDE * JELLI_HEIGHT };
static uint16_t pixels[COUNT + 2 * GUARD], expected[COUNT + 2 * GUARD];
static JelliPetUi ui;
static JelliGame game;
static JelliScene scene;

static void render(JelliSurface *surface) { jelli_pet_render(surface, &game, &ui, 0, false); }

static void restores_composite(JelliSurface *surface)
{
    memcpy(expected, pixels, sizeof(pixels));
    JelliRect region = {20, 180, 80, 80};
    for (unsigned y = region.y; y < region.y + region.height; ++y)
        for (unsigned x = region.x; x < region.x + region.width; ++x)
            surface->pixels[y * STRIDE + x] = 0;
    jelli_pet_draw_region(surface, &game, &ui, &ui.last_view, region);
    CHECK(!memcmp(expected, pixels, sizeof(pixels)));
    ui.particles.items[0] = (JelliParticle){.x = 100 * 16, .y = 200 * 16, .life = 8};
    ui.particles.changed = true;
    surface->damage = (JelliRect){0};
    jelli_pet_draw_particles(surface, &game, &ui);
    CHECK(memcmp(expected, pixels, sizeof(pixels)));
    ui.particles.items[0].life = 0;
    ui.particles.changed = true;
    surface->damage = (JelliRect){0};
    jelli_pet_draw_particles(surface, &game, &ui);
    CHECK(!memcmp(expected, pixels, sizeof(pixels)));
}

static void invalid_assets_preserve_cache(JelliSurface *surface)
{
    uint32_t revision = scene.revision;
    uint16_t first = scene.pixels[0];
    JelliAsset bad = {.id = scene.definition->layers[0].asset, .width = 1, .height = 1};
    JelliAssetSet set = {.items = &bad, .count = 1};
    ui.assets = &set;
    CHECK(!jelli_pet_scene_select(&ui, &scene, "market-street"));
    CHECK(scene.revision == revision && scene.pixels[0] == first);
    jelli_scene_advance(&scene, 256, 256, 256);
    render(surface);
    CHECK(scene.revision == revision && scene.pixels[0] == first);
    ui.assets = NULL; /* Invalid views are borrowed only during this call. */
    render(surface);
    CHECK(scene.revision != revision);
}

static void override_reload(JelliSurface *surface)
{
    static uint16_t colors[JELLI_SCENE_SIZE * JELLI_SCENE_SIZE];
    const JelliAsset *original = jelli_asset_find(scene.definition->layers[0].asset);
    CHECK(original != NULL);
    JelliAsset override = *original;
    override.pixels = colors;
    JelliAssetSet set = {.items = &override, .count = 1};
    for (unsigned i = 0; i < JELLI_SCENE_SIZE * JELLI_SCENE_SIZE; ++i)
        colors[i] = 0x1234;
    ui.assets = &set;
    render(surface);
    CHECK(scene.pixels[0] == 0x1234);
    colors[0] = 0x5678;
    ui.rendered = false; /* Same-address reload contract. */
    render(surface);
    CHECK(scene.pixels[0] == 0x5678);
    ui.assets = NULL;
    render(surface);
    CHECK(scene.pixels[0] != 0x5678);
}

static void background_sampling(JelliSurface *surface)
{
    JelliPetRenderKey view = ui.last_view;
    view.night = 0;
    jelli_pet_draw_background(surface, &view, (JelliRect){0, 0, 466, 466});
    CHECK(surface->pixels[0] == 0); /* Circular corners remain masked. */
    CHECK(surface->pixels[200 * STRIDE + 200] ==
          scene.pixels[(200u * 64u / 466u) * 64u + 200u * 64u / 466u]);
    for (unsigned y = 0; y < JELLI_HEIGHT; ++y)
        for (unsigned x = JELLI_WIDTH; x < STRIDE; ++x)
            CHECK(surface->pixels[y * STRIDE + x] == 0xdead);
    for (unsigned i = 0; i < GUARD; ++i)
        CHECK(pixels[i] == 0xdead && pixels[COUNT + GUARD + i] == 0xdead);
}

int main(void)
{
    for (unsigned i = 0; i < COUNT + 2 * GUARD; ++i)
        pixels[i] = 0xdead;
    JelliSurface surface = {pixels + GUARD, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    CHECK(jelli_pet_scene_select(&ui, &scene, "market-street"));
    render(&surface);
    CHECK(surface.damage.width == JELLI_WIDTH);
    render(&surface);
    CHECK(surface.damage.width == 0);
    uint32_t revision = scene.revision;
    jelli_scene_advance(&scene, 0, 0, 1); /* A subpixel delta should not redraw. */
    render(&surface);
    CHECK(scene.revision == revision && surface.damage.width == 0);
    jelli_scene_advance(&scene, 256, 0, 256);
    render(&surface);
    CHECK(surface.damage.width == JELLI_WIDTH && scene.revision != revision);
    CHECK(!jelli_pet_scene_select(&ui, &scene, "missing"));
    restores_composite(&surface);
    invalid_assets_preserve_cache(&surface);
    override_reload(&surface);
    background_sampling(&surface);
    CHECK(jelli_pet_scene_select(&ui, &scene, "garden-walk"));
    render(&surface);
    CHECK(surface.damage.width == JELLI_WIDTH);
    jelli_pet_scene_clear(&ui);
    render(&surface);
    CHECK(surface.damage.width == JELLI_WIDTH && !ui.last_view.scene_pixels);
    printf("Pet scene rendering/restore passed; UI=%zu, cache=%zu bytes\n", sizeof(ui),
           sizeof(scene));
    return 0;
}

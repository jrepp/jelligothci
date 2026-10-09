/* Full-frame render cost of the embedded art versus a high-resolution set: the embedded
 * art itself where it is already high-resolution, otherwise legacy art upscaled by
 * tests/hires_fixture.c. With the shipped assets both columns are the real art; for a
 * legacy baseline, build with -DJELLI_ASSET_SOURCE set to the output of
 * `tests/hires_fixture.py OUT --legacy`. A measurement tool, not a test: it reads the
 * process CPU clock. Build optimised for meaningful numbers.
 * Define BENCH_LEGACY_ONLY to compile against a renderer without density support. */
#include "jelli/pet_ui.h"
#ifndef BENCH_LEGACY_ONLY
#include "hires_fixture.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { STRIDE = JELLI_WIDTH, PIXELS = STRIDE * JELLI_HEIGHT, POOL = 1700000, FRAMES = 300 };
static uint16_t pixels[PIXELS];
#ifndef BENCH_LEGACY_ONLY
static uint16_t pool_pixels[POOL];
static uint8_t pool_alpha[POOL];
static JelliAsset fixtures[160];
#endif

typedef struct {
    const char *name;
    bool menu_open;
    JelliPetPage page;
} Scene;

/* Mean CPU microseconds per forced full redraw of one scene. */
static double measure(const Scene *scene, const JelliAssetSet *assets)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 64u};
    ui.assets = assets;
    ui.menu_open = scene->menu_open;
    ui.page = scene->page;
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, &game, &ui, 1000u, false); /* Settle ring animation. */
    jelli_pet_render(&surface, &game, &ui, 2000u, false);
    clock_t start = clock();
    for (unsigned i = 0u; i < FRAMES; ++i) {
        ui.rendered = false;
        jelli_pet_render(&surface, &game, &ui, 2000u, false);
    }
    return (double)(clock() - start) * 1e6 / (double)CLOCKS_PER_SEC / (double)(unsigned)FRAMES;
}

int main(void)
{
    static const Scene scenes[] = {{"home (creature 6x, tile, battery)", false, JELLI_UI_HOME},
                                   {"home ring (menus 3x, close 1x)", true, JELLI_UI_HOME},
                                   {"care ring (icons 6x, health 3x)", true, JELLI_UI_CARE},
                                   {"settings (battery 4x, gear 1x)", true, JELLI_UI_SETTINGS},
                                   {"collection (prizes 2x)", true, JELLI_UI_COLLECTION}};
#ifndef BENCH_LEGACY_ONLY
    HiresPool pool = {pool_pixels, pool_alpha, POOL, 0u};
    size_t count = hires_fixture_set(fixtures, sizeof(fixtures) / sizeof(fixtures[0]), &pool);
    if (!count)
        return 1;
    const JelliAssetSet hires = {fixtures, count, NULL};
    printf("%-36s %12s %12s\n", "scene (us per full frame)", "embedded", "high-res");
#else
    printf("%-36s %12s\n", "scene (us per full frame)", "embedded");
#endif
    for (size_t i = 0u; i < sizeof(scenes) / sizeof(scenes[0]); ++i) {
#ifndef BENCH_LEGACY_ONLY
        printf("%-36s %12.0f %12.0f\n", scenes[i].name, measure(&scenes[i], NULL),
               measure(&scenes[i], &hires));
#else
        printf("%-36s %12.0f\n", scenes[i].name, measure(&scenes[i], NULL));
#endif
    }
    return 0;
}

#include "jelli/pet_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
enum { STRIDE = JELLI_WIDTH + 5, PIXELS = STRIDE * JELLI_HEIGHT };
static uint16_t pixels[PIXELS], comparison[PIXELS];
static bool region_changed(unsigned left, unsigned top, unsigned width, unsigned height)
{
    for (unsigned y = top; y < top + height; ++y)
        for (unsigned x = left; x < left + width; ++x)
            if (pixels[y * STRIDE + x] != comparison[y * STRIDE + x])
                return true;
    return false;
}

static void test_battery_settings(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.menu_open = true;
    ui.page = JELLI_UI_SETTINGS;
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    memcpy(comparison, pixels, sizeof(comparison));
    ui.battery_present = true;
    ui.battery_voltage_valid = true;
    ui.battery_gauge_valid = true;
    ui.battery_mv = 3575;
    ui.battery_percent = 10;
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(surface.damage.width == JELLI_WIDTH);
    CHECK(region_changed(160u, 76u, 146u, 32u));
    memcpy(comparison, pixels, sizeof(comparison));
    ui.battery_voltage_valid = false;
    ui.battery_gauge_valid = false;
    ui.battery_present = false;
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(region_changed(160u, 76u, 146u, 32u));
}

int main(void)
{
    test_battery_settings();
    return 0;
}

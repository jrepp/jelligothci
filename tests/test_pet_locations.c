#include "jelli/locations.h"
#include "jelli/pet_ui.h"
#include "jelli/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STRIDE = JELLI_WIDTH + 3 };
static uint16_t pixels[STRIDE * JELLI_HEIGHT];

int main(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    uint32_t hashes[JELLI_LOCATION_CAPACITY] = {0};
    for (unsigned i = 0u; i < jelli_location_count; ++i) {
        const JelliAsset *asset = jelli_asset_find(jelli_locations[i].background);
        CHECK(asset && asset->width == 64u && asset->height == 64u);
        game.pets[0].location = (uint8_t)i;
        memset(pixels, 0xa5, sizeof(pixels));
        /* Reset history because this loop intentionally replaces the pixel buffer. */
        jelli_pet_ui_init(&ui);
        jelli_pet_render(&surface, &game, &ui, 0u, false);
        CHECK(surface.damage.width == JELLI_WIDTH && surface.damage.height == JELLI_HEIGHT);
        CHECK(pixels[0] == 0u);
        for (unsigned y = 0u; y < JELLI_HEIGHT; ++y) {
            for (unsigned x = 0u; x < JELLI_WIDTH; ++x)
                hashes[i] =
                    hashes[i] * 31u + pixels[y * STRIDE + x]; /* Intentional wrapping hash. */
            for (unsigned x = JELLI_WIDTH; x < STRIDE; ++x)
                CHECK(pixels[y * STRIDE + x] == 0xa5a5u);
        }
        for (unsigned previous = 0u; previous < i; ++previous)
            CHECK(hashes[previous] != hashes[i]);
        jelli_pet_render(&surface, &game, &ui, 100u, false);
        CHECK(surface.damage.width == 0u && surface.damage.height == 0u);
        game.pets[0].location = (uint8_t)((i + 1u) % jelli_location_count);
        jelli_pet_render(&surface, &game, &ui, 100u, false);
        CHECK(surface.damage.width == JELLI_WIDTH && surface.damage.height == JELLI_HEIGHT);
    }
    puts("PASS: distinct location art, travel damage, unchanged frames and stride bounds");
    return 0;
}

#include "jelli/pet_effect.h"
#include "../core/pet_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%d: %s\n", __LINE__, #expr);                                          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT], saved[JELLI_WIDTH * JELLI_HEIGHT];
static JelliPetUi ui;
static JelliGame game;
int main(void)
{
    JelliSurface s = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    CHECK(jelli_pet_effect_start(&ui, "celebrate", 0u));
    const JelliEffectDefinition *d = ui.effect;
    CHECK(!jelli_pet_effect_start(&ui, "missing", 0u) && ui.effect == d);
    jelli_pet_render(&s, &game, &ui, 600u, false);
    CHECK(ui.last_view.pose == JELLI_POSE_HAPPY && ui.effect_sample.count > 0u);
    memcpy(saved, pixels, sizeof(saved));
    jelli_pet_render(&s, &game, &ui, 1000u, true);
    CHECK(ui.last_view.effect_tick == 601u);
    memcpy(saved, pixels, sizeof(saved));
    jelli_pet_render(&s, &game, &ui, 1100u, true);
    CHECK(!memcmp(saved, pixels, sizeof(saved)));
    jelli_pet_render(&s, &game, &ui, 1200u, false);
    CHECK(memcmp(saved, pixels, sizeof(saved)) != 0);
    memcpy(saved, pixels, sizeof(saved));
    JelliRect region = {100, 100, 266, 166};
    jelli_pet_draw_region(&s, &game, &ui, &ui.last_view, region);
    CHECK(!memcmp(saved, pixels, sizeof(saved)));
    jelli_pet_render(&s, &game, &ui, 3000u, false);
    CHECK(!ui.effect && !ui.effect_sample.active && s.damage.width == JELLI_WIDTH);
    CHECK(jelli_pet_effect_start(&ui, "celebrate", 4000u));
    jelli_pet_render(&s, &game, &ui, 3999u, false);
    CHECK(!ui.effect);
    CHECK(jelli_pet_effect_start(&ui, "celebrate", 0u));
    ui.menu_open = true;
    jelli_pet_render(&s, &game, &ui, 500u, false);
    CHECK(!ui.effect);
    puts("pet effect lifecycle passed");
    return 0;
}

#include "jelli/pet_scene.h"
#include "jelli/activities.h"
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
static JelliPetUi ui;
static JelliGame game;
static unsigned char before[sizeof(JelliGame)];
static JelliScene explicit_scene;
static uint16_t original[JELLI_SCENE_SIZE * JELLI_SCENE_SIZE];

static void coverage(void)
{
    JelliPet *pet = &game.pets[game.active];
    for (unsigned i = 0; i < jelli_moment_count; ++i) {
        pet->moment = (uint8_t)(i + 1u);
        pet->activity = JELLI_PLAYING;
        memcpy(before, &game, sizeof(game));
        jelli_pet_activity_scene(&ui, &game, (uint64_t)i * 1000u, false);
        CHECK(ui.scene == &ui.activity_scene);
        CHECK(strcmp(ui.scene->definition->activity, jelli_moments[i].name) == 0);
        CHECK(memcmp((const unsigned char *)&game, before, sizeof(game)) == 0);
        memcpy(original, ui.activity_scene.pixels, sizeof(original));
        bool moved = false;
        for (unsigned step = 1; step <= 64; ++step) {
            jelli_pet_activity_scene(&ui, &game, (uint64_t)i * 1000u + (uint64_t)step * 250u,
                                     false);
            jelli_pet_scene_prepare(&ui);
            moved |= memcmp(original, ui.activity_scene.pixels, sizeof(original)) != 0;
        }
        CHECK(moved);
    }
    pet->moment = 0;
    pet->activity = JELLI_EXERCISING;
    jelli_pet_activity_scene(&ui, &game, 30000, false);
    CHECK(strcmp(ui.scene->definition->activity, "WORKOUT") == 0);
}

static void lifecycle(void)
{
    JelliPet *pet = &game.pets[game.active];
    pet->moment = 1;
    pet->activity = JELLI_PLAYING;
    jelli_pet_activity_scene(&ui, &game, 31000, false);
    CHECK(ui.scene == &ui.activity_scene);
    CHECK(jelli_pet_scene_select(&ui, &explicit_scene, "garden-walk"));
    pet->moment = 2;
    jelli_pet_activity_scene(&ui, &game, 32000, false);
    CHECK(ui.scene == &explicit_scene);
    jelli_pet_scene_clear(&ui);
    jelli_pet_activity_scene(&ui, &game, 33000, false);
    CHECK(strcmp(ui.scene->definition->activity, "TEA") == 0);
    pet->activity = JELLI_IDLE;
    jelli_pet_activity_scene(&ui, &game, 34000, false);
    CHECK(!ui.scene);
    pet->activity = JELLI_PLAYING;
    game.resuming = true;
    jelli_pet_activity_scene(&ui, &game, 35000, false);
    CHECK(!ui.scene);
    game.resuming = false;
    jelli_pet_activity_scene(&ui, &game, 36000, false);
    CHECK(ui.scene);
}

static void timing(void)
{
    jelli_pet_scene_clear(&ui);
    jelli_pet_activity_scene(&ui, &game, 0, false);
    int32_t initial = ui.activity_scene.phase[1];
    for (unsigned ms = 1; ms <= 1000; ++ms)
        jelli_pet_activity_scene(&ui, &game, ms, false);
    CHECK(ui.activity_scene.phase[1] == 256 * 60);
    jelli_pet_activity_scene(&ui, &game, 10000, false);
    CHECK(ui.activity_scene.phase[1] == 320 * 60); /* Stall capped at 250ms. */
    jelli_pet_activity_scene(&ui, &game, 20000, true);
    CHECK(ui.activity_scene.phase[1] == 320 * 60);
    jelli_pet_activity_scene(&ui, &game, 20001, false);
    CHECK(ui.activity_scene.phase[1] == 320 * 60);
    jelli_pet_activity_scene(&ui, &game, 0, false);
    CHECK(ui.activity_scene.phase[1] == 320 * 60);
    game.pets[game.active].interaction_due++;
    jelli_pet_activity_scene(&ui, &game, 1, false);
    CHECK(ui.activity_scene.phase[1] == initial);
    game.pets[game.active].id++;
    jelli_pet_activity_scene(&ui, &game, 100, false);
    CHECK(ui.activity_scene.phase[1] == 0);
}

static void missing_art(void)
{
    JelliAsset bad = {.id = ui.scene->definition->layers[0].asset, .width = 1, .height = 1};
    JelliAssetSet set = {.items = &bad, .count = 1};
    ui.assets = &set;
    game.pets[game.active].moment = 3;
    jelli_pet_activity_scene(&ui, &game, 200, false);
    CHECK(!ui.scene);
    ui.assets = NULL;
    jelli_pet_activity_scene(&ui, &game, 300, false);
    CHECK(ui.scene);
    CHECK(strcmp(ui.scene->definition->activity, "GOING OUT") == 0);
}

int main(void)
{
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    coverage();
    lifecycle();
    timing();
    missing_art();
    puts("activity scene tests passed");
    return 0;
}

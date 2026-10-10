#include "jelli/nutrition.h"
#include "jelli/save.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void water_and_time(void)
{
    JelliGame game;
    jelli_game_init(&game);
    JelliPet *pet = &game.pets[0];
    CHECK(pet->hydration == 700u);
    JelliPet many = *pet, one = *pet;
    for (unsigned i = 0u; i < 601u; ++i)
        jelli_hydration_advance(&many, 1u);
    jelli_hydration_advance(&one, 601u);
    CHECK(many.hydration == 699u && many.hydration == one.hydration);
    CHECK(many.hydration_remainder == one.hydration_remainder);
    one.asleep = true;
    jelli_hydration_advance(&one, 2400u);
    CHECK(one.hydration == 698u);
    jelli_hydration_advance(&one, UINT64_MAX);
    CHECK(one.hydration == 0u && one.hydration_remainder == 0u);
    pet->hydration = 0u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_WATER, 1u, 0u}) == JELLI_OK);
    CHECK(pet->hydration == 1000u && game.food == 5u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_WATER, 1u, 0u}) == JELLI_FULL);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_PLAY, 1u, 0u}) == JELLI_OK);
    pet->hydration = 400u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_WATER, 1u, 0u}) == JELLI_BUSY);
    CHECK(pet->hydration == 400u);
    pet->activity = JELLI_IDLE;
    pet->interaction_due = 0u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, 1u, 0u}) == JELLI_OK);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_WATER, 1u, 0u}) == JELLI_ASLEEP);
}

static void hydration_save(void)
{
    JelliSave save = {0}, loaded;
    jelli_game_init(&save.game);
    save.game.pets[0].hydration = 123u;
    save.game.pets[0].hydration_remainder = 2399u;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].hydration == 123u &&
          loaded.game.pets[0].hydration_remainder == 2399u);
    uint16_t stored = loaded.game.pets[1].hydration;
    for (unsigned i = 0u; i < 100u; ++i)
        jelli_game_advance(&loaded.game, 800u);
    CHECK(loaded.game.pets[1].hydration == stored);
    loaded.game.pets[0].hydration = 1001u;
    CHECK(!jelli_game_valid(&loaded.game));
}

int main(void)
{
    water_and_time();
    hydration_save();
    puts("PASS: hydration timing, saturation, water guards and save continuity");
    return 0;
}

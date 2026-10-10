#include "game_fixture.h"
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

static void advance(JelliGame *game, unsigned ticks)
{
    for (unsigned i = 0u; i < ticks; ++i)
        jelli_game_advance(game, 100u);
}

static void workout_and_resume(void)
{
    JelliSave save = {0}, loaded;
    test_game_pair(&save.game);
    JelliPet *pet = &save.game.pets[0];
    unsigned food = pet->needs[JELLI_SATIETY], water = pet->hydration;
    unsigned energy = pet->needs[JELLI_ENERGY];
    JelliCommand workout = {JELLI_CMD_EXERCISE, pet->id, 0u};
    CHECK(jelli_game_command(&save.game, workout) == JELLI_OK);
    CHECK(pet->activity == JELLI_EXERCISING);
    CHECK(pet->needs[JELLI_SATIETY] == food - jelli_exercise.fullness_cost);
    CHECK(pet->hydration == water - jelli_exercise.hydration_cost);
    CHECK(pet->needs[JELLI_ENERGY] == energy - 65u);
    CHECK(jelli_game_command(&save.game, workout) == JELLI_BUSY);
    CHECK(pet->hydration == water - jelli_exercise.hydration_cost);
    CHECK(save.game.food == 5u);
    advance(&save.game, jelli_exercise.duration_ticks / 2u);
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].activity == JELLI_EXERCISING);
    CHECK(loaded.game.pets[0].hydration == pet->hydration);
    unsigned amusement = loaded.game.pets[0].needs[JELLI_AMUSEMENT];
    advance(&loaded.game, jelli_exercise.duration_ticks);
    CHECK(loaded.game.pets[0].activity == JELLI_IDLE);
    CHECK(loaded.game.pets[0].hydration >= pet->hydration - 1u);
    CHECK(loaded.game.pets[0].needs[JELLI_AMUSEMENT] > amusement);
    CHECK(loaded.game.pets[1].hydration == save.game.pets[1].hydration);
    unsigned done = loaded.game.pets[0].needs[JELLI_AMUSEMENT];
    advance(&loaded.game, jelli_exercise.duration_ticks);
    CHECK(loaded.game.pets[0].needs[JELLI_AMUSEMENT] <= done);
}

static void workout_guards(void)
{
    JelliGame game;
    test_game_pair(&game);
    JelliPet *pet = &game.pets[0];
    JelliCommand workout = {JELLI_CMD_EXERCISE, pet->id, 0u};
    pet->hydration = (uint16_t)(jelli_exercise.hydration_cost - 1u);
    unsigned food = pet->needs[JELLI_SATIETY];
    CHECK(jelli_game_command(&game, workout) == JELLI_NOT_READY);
    CHECK(pet->needs[JELLI_SATIETY] == food && pet->activity == JELLI_IDLE);
    pet->hydration = 1000u;
    pet->needs[JELLI_SATIETY] = (uint16_t)(jelli_exercise.fullness_cost - 1u);
    CHECK(jelli_game_command(&game, workout) == JELLI_NOT_READY);
    CHECK(pet->hydration == 1000u);
    pet->needs[JELLI_SATIETY] = 1000u;
    pet->needs[JELLI_ENERGY] = (uint16_t)(jelli_exercise.energy_cost - 1u);
    CHECK(jelli_game_command(&game, workout) == JELLI_NOT_READY);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, pet->id, 0u}) == JELLI_OK);
    CHECK(jelli_game_command(&game, workout) == JELLI_ASLEEP);
    test_game_pair(&game);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_CARE, pet->id, 0u}) == JELLI_OK);
    CHECK(jelli_game_command(&game, workout) == JELLI_BUSY);
    CHECK(jelli_game_valid(&game));
}

int main(void)
{
    workout_and_resume();
    workout_guards();
    puts("PASS: workout costs once, save continuity, completion and resource guards");
    return 0;
}

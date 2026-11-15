#include "jelli/activities.h"
#include "jelli/save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef JELLI_TEST_V21_FIXTURE
#define JELLI_TEST_V21_FIXTURE "tests/fixtures/save-v21-before-rpg.bin"
#endif
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void repair(uint8_t *bytes, size_t size)
{
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (8u * i));
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(~crc >> (8u * i));
}

static void real_v21_checkpoint(void)
{
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    FILE *file = fopen(JELLI_TEST_V21_FIXTURE, "rb");
    CHECK(file != NULL);
    size_t size = fread(bytes, 1u, sizeof(bytes), file);
    CHECK(fclose(file) == 0 && size > 0u && bytes[4] == 21u);
    JelliSave loaded;
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.sequence == 42u && loaded.game.pets[0].rpg_stats[0] == 0u &&
          loaded.game.pets[0].rpg_stats[1] == 0u && loaded.game.pets[0].rpg_stats[2] == 0u);
}

int main(void)
{
    real_v21_checkpoint();
    JelliSave save = {0}, loaded;
    jelli_game_init(&save.game);
    JelliPet *pet = &save.game.pets[0];
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        pet->needs[i] = 700u;
    pet->hydration = 700u;
    CHECK(jelli_moments[4].rpg_gains[JELLI_RPG_INTELLIGENCE] == 10u);
    unsigned base = jelli_moment_cost(pet, 4u, JELLI_ENERGY);
    pet->rpg_stats[JELLI_RPG_STRENGTH] = 1000u;
    CHECK(jelli_moment_cost(pet, 4u, JELLI_ENERGY) == base);
    pet->rpg_stats[JELLI_RPG_INTELLIGENCE] = 1000u;
    CHECK(jelli_moment_cost(pet, 4u, JELLI_ENERGY) == (base * 80u + 99u) / 100u);
    pet->rpg_stats[JELLI_RPG_INTELLIGENCE] = 995u;
    CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_MOMENT, pet->id, 4u}) ==
          JELLI_OK);
    for (unsigned i = 0u; i < 20u; ++i)
        jelli_game_advance(&save.game, 800u);
    CHECK(pet->rpg_stats[JELLI_RPG_INTELLIGENCE] == 1000u);
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].rpg_stats[JELLI_RPG_INTELLIGENCE] == 1000u);
    pet->rpg_stats[0] = 1001u;
    CHECK(!jelli_game_valid(&save.game));
    CHECK(jelli_save_encode(&save, bytes, sizeof(bytes)) == 0u);
    pet->rpg_stats[0] = 1000u;
    size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size > 0u);
    size_t tail = (size_t)save.game.count * 6u;
    memmove(bytes + size - 8u - tail, bytes + size - 8u, 8u);
    size -= tail;
    bytes[4] = 21u;
    repair(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].rpg_stats[0] == 0u && loaded.game.pets[0].rpg_stats[2] == 0u);
    return 0;
}

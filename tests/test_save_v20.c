#include "game_fixture.h"
#include "jelli/save.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static JelliSave fixture(void)
{
    JelliSave save = {0};
    test_game_pair(&save.game);
    save.game.pets[0].reward_claimed = true;
    save.game.pets[1].reward_pending = true;
    return save;
}

static size_t encode(const JelliSave *save, uint8_t *bytes)
{
    size_t size = jelli_save_encode(save, bytes, JELLI_SAVE_CAPACITY);
    CHECK(size != 0u);
    return size;
}

static void repair_checksum(uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(crc >> (8u * i));
}

static void advance_ms(JelliGame *game, uint64_t milliseconds)
{
    while (milliseconds > 0u) {
        uint64_t step = milliseconds > 800u ? 800u : milliseconds;
        jelli_game_advance(game, step);
        milliseconds -= step;
    }
}

static void local_v20_unlimited_food_migration(void)
{
    JelliSave original = fixture(), loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    original.game.food = 1u;
    original.game.pets[0].activity = JELLI_EATING;
    original.game.pets[0].interaction_due = original.game.pets[0].ticks + 50u;
    size_t size = encode(&original, bytes);
    /* The local writer could save an in-flight meal with an empty inventory. */
    bytes[64] = 0u;
    bytes[65] = 0u;
    repair_checksum(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.food == 0u && loaded.game.pets[0].activity == JELLI_EATING);
    CHECK(loaded.game.pets[0].reward_claimed && loaded.game.pets[1].reward_pending);
    bytes[4] = 19u;
    repair_checksum(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    bytes[64] = 1u;
    repair_checksum(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.food == 1u && loaded.game.pets[0].activity == JELLI_EATING);
}

static void local_v20_streamed_meal_completes(void)
{
    JelliSave original = fixture(), loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    original.game.food = 1u;
    CHECK(jelli_game_command(&original.game,
                             (JelliCommand){.kind = JELLI_CMD_FEED, .actor_id = 1u}) == JELLI_OK);
    CHECK(original.game.food == 1u && original.game.pets[0].moment_pulse != 0u);
    uint32_t meals = original.game.pets[0].habits.lifetime_meals;
    uint16_t satiety = original.game.pets[0].needs[JELLI_SATIETY];
    size_t size = encode(&original, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.food == 1u);
    advance_ms(&loaded.game, 5000u);
    CHECK(loaded.game.pets[0].activity == JELLI_IDLE);
    CHECK(loaded.game.pets[0].habits.lifetime_meals == meals + 1u);
    CHECK(loaded.game.pets[0].needs[JELLI_SATIETY] > satiety);
}

static void legacy_food_type_completes(void)
{
    JelliSave original = fixture(), loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    original.game.food = 0u;
    original.game.pets[0].activity = JELLI_EATING;
    original.game.pets[0].food_type = 1u;
    original.game.pets[0].interaction_due = original.game.pets[0].ticks + 50u;
    uint32_t meals = original.game.pets[0].habits.lifetime_meals;
    size_t size = encode(&original, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.food == 0u);
    advance_ms(&loaded.game, 5000u);
    CHECK(loaded.game.pets[0].activity == JELLI_IDLE);
    CHECK(loaded.game.pets[0].habits.lifetime_meals == meals + 1u);
}

int main(void)
{
    local_v20_unlimited_food_migration();
    local_v20_streamed_meal_completes();
    legacy_food_type_completes();
    puts("PASS: local format 20 migration");
    return 0;
}

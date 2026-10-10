#include "jelli/activities.h"
#include "jelli/locations.h"
#include "jelli/save.h"
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

static void ready(JelliGame *game)
{
    jelli_game_init(game);
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        game->pets[0].needs[i] = 500u;
    game->pets[0].hydration = 500u;
    game->clock_known = true;
    game->clock_minute = 800u;
}

static JelliResult reading(JelliGame *game)
{
    return jelli_game_command(game, (JelliCommand){JELLI_CMD_MOMENT, game->pets[0].id, 4u});
}

static void finish(JelliGame *game)
{
    for (unsigned i = 0u; i < 10u; ++i)
        jelli_game_advance(game, 800u);
}

static void unchanged(const JelliGame *before, const JelliGame *after)
{
    JelliSave first = {.game = *before}, second = {.game = *after};
    uint8_t a[JELLI_SAVE_CAPACITY], b[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&first, a, sizeof(a));
    CHECK(size && jelli_save_encode(&second, b, sizeof(b)) == size);
    CHECK(memcmp(a, b, size) == 0);
    CHECK(before->pets[0].random_state == after->pets[0].random_state);
}

static void costs_and_interruptions(void)
{
    JelliGame game, before, scratch;
    ready(&game);
    before = game;
    CHECK(jelli_game_check(&game, (JelliCommand){JELLI_CMD_MOMENT, 1u, 4u}, &scratch) == JELLI_OK);
    unchanged(&before, &game);
    CHECK(reading(&game) == JELLI_OK);
    CHECK(game.pets[0].needs[JELLI_ENERGY] == 422u && game.pets[0].hydration == 440u);
    CHECK(game.pets[0].needs[JELLI_AMUSEMENT] == 500u && game.pets[0].bond == before.pets[0].bond);
    before = game;
    CHECK(reading(&game) == JELLI_BUSY);
    unchanged(&before, &game);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_CARE, 1u, 0u}) == JELLI_OK);
    finish(&game);
    CHECK(game.pets[0].needs[JELLI_AMUSEMENT] <= 500u);
    CHECK(game.pets[0].hydration <= 440u && game.pets[0].bond == before.pets[0].bond);
    CHECK(!(game.pets[0].completed_moments & (UINT32_C(1) << 4u)));
    ready(&game);
    game.pets[0].hydration = 59u;
    before = game;
    CHECK(reading(&game) == JELLI_NOT_READY);
    CHECK(strcmp(jelli_moment_hint(&game, &game.pets[0], 4u), "DRINK FIRST") == 0);
    unchanged(&before, &game);
    game.pets[0].hydration = 60u;
    CHECK(reading(&game) == JELLI_OK && game.pets[0].hydration == 0u);
}

static void saved_roll_and_completion(void)
{
    JelliSave save = {0}, loaded;
    ready(&save.game);
    CHECK(reading(&save.game) == JELLI_OK);
    unsigned roll = jelli_moment_jitter_percent(&save.game.pets[0], 4u);
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(jelli_moment_jitter_percent(&loaded.game.pets[0], 4u) == roll);
    finish(&save.game);
    jelli_game_resume_begin(&loaded.game, 8000u);
    CHECK(jelli_game_resume_step(&loaded.game));
    CHECK(loaded.game.pets[0].completed_moments == 0u);
    CHECK(save.game.pets[0].needs[JELLI_AMUSEMENT] == loaded.game.pets[0].needs[JELLI_AMUSEMENT]);
    CHECK(save.game.pets[0].hydration == loaded.game.pets[0].hydration);
    unsigned reward = save.game.pets[0].needs[JELLI_AMUSEMENT];
    CHECK(reward >= 680u && reward <= 755u);
    finish(&save.game);
    CHECK(save.game.pets[0].needs[JELLI_AMUSEMENT] <= reward);
}

static void bounded_variation_and_bonuses(void)
{
    JelliGame game;
    ready(&game);
    JelliPet *pet = &game.pets[0];
    uint32_t seen = 0u, random = pet->random_state;
    for (unsigned i = 0u; i < 500u; ++i) {
        pet->interaction_due = UINT64_C(80) * (i + 1u);
        unsigned roll = jelli_moment_jitter_percent(pet, 4u);
        CHECK(roll >= 85u && roll <= 115u);
        seen |= UINT32_C(1) << (roll - 85u);
    }
    CHECK(seen == UINT32_C(0x7fffffff) && pet->random_state == random);
    pet->form = 0u;
    CHECK(jelli_moment_bonus_percent(pet, 4u) == 100u);
    pet->form = 2u;
    CHECK(jelli_moment_bonus_percent(pet, 4u) == 150u);
    CHECK(jelli_moment_bonus_percent(pet, 19u) == 140u);
    pet->form = 1u;
    CHECK(jelli_moment_bonus_percent(pet, 14u) == 135u);
}

static void pet_costs_locations_and_age(void)
{
    JelliGame game;
    ready(&game);
    JelliPet *pet = &game.pets[0];
    CHECK(jelli_moment_cost(pet, 4u, JELLI_ENERGY) == 78u);
    CHECK(jelli_moment_cost(pet, 4u, JELLI_ACTIVITY_HYDRATION) == 60u);
    CHECK(strcmp(jelli_moment_hint(&game, pet, 13u), "FOR ANOTHER PET") == 0);
    pet->form = 2u;
    CHECK(jelli_moment_cost(pet, 4u, JELLI_ENERGY) == 60u);
    CHECK(jelli_moment_cost(pet, 4u, JELLI_ACTIVITY_HYDRATION) == 90u);
    CHECK(jelli_moment_available(&game, pet, 13u) == JELLI_NOT_READY); /* Hourly offer. */
    unsigned seen = 0u;
    for (unsigned i = 0u; i < 50u; ++i) {
        pet->interaction_due = (uint64_t)i * 80u + 80u;
        unsigned location = jelli_moment_location(pet, 4u);
        CHECK(location < jelli_location_count && location == jelli_moment_location(pet, 4u));
        CHECK(jelli_moments[4].locations & (1u << location));
        seen |= 1u << location;
        CHECK(jelli_moment_location(pet, 16u) == 1u); /* Gardening stays outdoors. */
    }
    CHECK(seen == jelli_moments[4].locations);
    ready(&game);
    unsigned bond = pet->bond;
    CHECK(reading(&game) == JELLI_OK);
    unsigned location = pet->location;
    finish(&game);
    CHECK(pet->bond > bond && pet->bond <= bond + 4u);
    CHECK(pet->location == location);
}

static void migrate_old_running_recipe(void)
{
    JelliSave save = {0}, loaded;
    ready(&save.game);
    CHECK(reading(&save.game) == JELLI_OK);
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size > 8u);
    bytes[4] = 12u; /* Same layout, old immediate-reward semantics. */
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(~crc >> (i * 8u));
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].moment == 0u && loaded.game.pets[0].activity == JELLI_IDLE);
    CHECK(loaded.game.pets[0].needs[JELLI_AMUSEMENT] == 500u);
    CHECK(jelli_game_valid(&loaded.game));
}

int main(void)
{
    pet_costs_locations_and_age();
    costs_and_interruptions();
    saved_roll_and_completion();
    bounded_variation_and_bonuses();
    migrate_old_running_recipe();
    puts("PASS: activity costs, completion rewards, pet bonuses, saved jitter and migration");
    return 0;
}

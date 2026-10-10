#include "jelli/activities.h"
#include "jelli/potty.h"
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

static JelliResult moment(JelliGame *game, unsigned id)
{
    game->clock_known = true;
    for (unsigned minute = jelli_moments[id < jelli_moment_count ? id : 0u].start_minute;
         minute < 1440u * 14u; ++minute) {
        game->pets[0].ticks = (uint64_t)(minute / 1440u) * JELLI_DAY_TICKS;
        game->clock_minute = (uint16_t)(minute % 1440u);
        if (jelli_moment_available(game, &game->pets[0], id) != JELLI_NOT_READY)
            break;
    }
    return jelli_game_command(game, (JelliCommand){JELLI_CMD_MOMENT, game->pets[0].id, id});
}

static void finish_activity(JelliGame *game)
{
    for (unsigned i = 0u; i < 40u && game->pets[0].activity != JELLI_IDLE; ++i)
        jelli_game_advance(game, 800u);
    CHECK(game->pets[0].activity == JELLI_IDLE);
}

/* The former hard-coded moments, now content data, keep their exact effects. */
static void legacy_moments_unchanged(void)
{
    static const struct {
        unsigned id;
        int need, gain, location;
    } legacy[] = {{1u, JELLI_SOCIAL, 60, -1},
                  {1u, JELLI_ENERGY, 100, -1},
                  {2u, -1, 0, 1},
                  {3u, JELLI_SOCIAL, 100, -1}};
    for (unsigned i = 0u; i < sizeof(legacy) / sizeof(legacy[0]); ++i) {
        JelliGame game, base;
        jelli_game_init(&game);
        game.pets[0].needs[JELLI_SOCIAL] = game.pets[0].needs[JELLI_ENERGY] = 300u;
        base = game;
        CHECK(moment(&game, legacy[i].id) == JELLI_OK);
        CHECK(game.pets[0].activity == JELLI_PLAYING && game.pets[0].moment == legacy[i].id + 1u);
        if (legacy[i].need >= 0)
            CHECK(game.pets[0].needs[legacy[i].need] >=
                  base.pets[0].needs[legacy[i].need] + legacy[i].gain);
        if (legacy[i].location >= 0)
            CHECK(game.pets[0].location == (uint8_t)legacy[i].location);
    }
    JelliGame game;
    jelli_game_init(&game);
    CHECK(moment(&game, 0u) == JELLI_OK && game.pets[0].activity == JELLI_EATING);
    CHECK(game.pets[0].moment == 1u); /* Meals retain their authored identity until completion. */
}

static void reading_moment(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].needs[JELLI_AMUSEMENT] = game.pets[0].needs[JELLI_SOCIAL] = 200u;
    CHECK(strcmp(jelli_moments[4].name, "READING") == 0 && jelli_moments[4].prop != 0u);
    CHECK(moment(&game, 4u) == JELLI_OK && game.pets[0].moment == 5u);
    CHECK(game.pets[0].needs[JELLI_AMUSEMENT] > 200u && game.pets[0].needs[JELLI_SOCIAL] > 200u);
    CHECK(moment(&game, 4u) == JELLI_BUSY);
    finish_activity(&game);
    CHECK(game.pets[0].moment == 0u && jelli_game_valid(&game));
    CHECK(moment(&game, jelli_moment_count) == JELLI_INVALID_TARGET);
}

static void potty_cycle(void)
{
    JelliGame game;
    jelli_game_init(&game);
    JelliPet *pet = &game.pets[0];
    CHECK(
        jelli_game_command(&game, (JelliCommand){JELLI_CMD_HEALTH, pet->id, JELLI_HEALTH_POTTY}) ==
        JELLI_FULL); /* Nothing to do yet. */
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, pet->id, 0u}) == JELLI_OK);
    finish_activity(&game);
    CHECK(pet->digesting == jelli_potty_rules.per_meal && pet->potty == 0u);
    pet->hydration = 0u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_WATER, pet->id, 0u}) == JELLI_OK);
    unsigned pool = jelli_potty_rules.per_meal + jelli_potty_rules.per_drink;
    CHECK(pet->digesting == pool);
    /* Digestion arrives a little each minute rather than at once. */
    uint64_t old = pet->ticks;
    pet->ticks += 600u;
    CHECK(!jelli_potty_advance(pet, old));
    CHECK(pet->potty == jelli_potty_rules.drain_per_minute);
    CHECK(pet->digesting == pool - jelli_potty_rules.drain_per_minute);
    pet->digesting = (uint16_t)jelli_potty_rules.urge_threshold; /* About two meals' worth. */
    bool crossed = false;
    for (unsigned minute = 0u; minute < 60u && !crossed; ++minute) {
        old = pet->ticks;
        pet->ticks += 600u;
        crossed = jelli_potty_advance(pet, old);
    }
    CHECK(crossed && pet->potty >= jelli_potty_rules.urge_threshold);
    old = pet->ticks;
    pet->ticks += 600u;
    CHECK(!jelli_potty_advance(pet, old)); /* The urge stimulus is an edge, not a level. */
    pet->needs[JELLI_HYGIENE] = 500u;
    JelliSave save = {.game = game}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].potty == pet->potty &&
          loaded.game.pets[0].digesting == pet->digesting);
    CHECK(jelli_game_command(
              &game, (JelliCommand){JELLI_CMD_HEALTH, pet->id, JELLI_HEALTH_POTTY}) == JELLI_OK);
    CHECK(pet->potty == 0u && pet->needs[JELLI_HYGIENE] == 500u + jelli_potty_rules.hygiene_gain);
    pet->digesting = 1000u;
    pet->potty = 990u;
    old = pet->ticks;
    pet->ticks += UINT64_C(600) * 100u;
    (void)jelli_potty_advance(pet, old);
    CHECK(pet->potty == 1000u && jelli_game_valid(&game)); /* Saturates at the cap. */
}

/* Favourite moments from data match the former ID-parity rule at every minute. */
static void favorites_match_legacy(void)
{
    JelliPet pet = {0};
    for (uint32_t id = 1u; id <= 4u; ++id) {
        pet.id = id;
        for (unsigned minute = 0u; minute < 1440u; ++minute) {
            unsigned legacy = (id & 1u) ? (minute < 660u ? 0u : 1u) : (minute < 1140u ? 2u : 3u);
            CHECK(jelli_pet_favorite(&pet, minute) == legacy);
        }
    }
}

static void timed_unlocks_and_dessert(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.clock_known = true;
    static const unsigned windows[][3] = {
        {0u, 360u, 660u}, {1u, 780u, 960u}, {5u, 660u, 840u}, {6u, 960u, 1260u}};
    for (unsigned i = 0u; i < 4u; ++i) {
        unsigned id = windows[i][0];
        game.clock_minute = (uint16_t)(windows[i][1] - 1u);
        CHECK(jelli_moment_available(&game, &game.pets[0], id) == JELLI_NOT_READY);
        game.clock_minute++;
        CHECK(jelli_moment_available(&game, &game.pets[0], id) == JELLI_OK);
        game.clock_minute = (uint16_t)(windows[i][2] - 1u);
        CHECK(jelli_moment_available(&game, &game.pets[0], id) == JELLI_OK);
        game.clock_minute++;
        CHECK(jelli_moment_available(&game, &game.pets[0], id) == JELLI_NOT_READY);
    }
    game.clock_minute = 1000u;
    CHECK(jelli_moment_available(&game, &game.pets[0], 7u) == JELLI_NOT_READY);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, 1u, 6u}) == JELLI_OK);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_CARE, 1u, 0u}) == JELLI_OK);
    finish_activity(&game);
    CHECK(jelli_moment_available(&game, &game.pets[0], 7u) == JELLI_NOT_READY);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, 1u, 6u}) == JELLI_OK);
    finish_activity(&game);
    CHECK(jelli_moment_available(&game, &game.pets[0], 7u) == JELLI_OK);
    JelliSave save = {.game = game}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(jelli_moment_available(&loaded.game, &loaded.game.pets[0], 7u) == JELLI_OK);
    loaded.game.pets[0].ticks += JELLI_DAY_TICKS;
    CHECK(jelli_moment_available(&loaded.game, &loaded.game.pets[0], 7u) == JELLI_NOT_READY);
}

static void random_offers_are_stable(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.clock_known = true;
    uint32_t seen = 0u;
    for (unsigned hour = 0u; hour < 168u; ++hour) {
        game.clock_minute = (uint16_t)(hour % 24u * 60u);
        game.pets[0].ticks = (uint64_t)(hour / 24u) * JELLI_DAY_TICKS;
        unsigned offered = 0u;
        uint32_t random = game.pets[0].random_state;
        for (unsigned id = 0u; id < jelli_moment_count; ++id) {
            if (!jelli_moments[id].random_weight)
                continue;
            JelliResult result = jelli_moment_available(&game, &game.pets[0], id);
            CHECK(result == jelli_moment_available(&game, &game.pets[0], id));
            if (result == JELLI_OK) {
                ++offered;
                seen |= UINT32_C(1) << id;
            }
        }
        CHECK(offered == 1u && game.pets[0].random_state == random);
    }
    for (unsigned id = 0u; id < jelli_moment_count; ++id)
        if (jelli_moments[id].random_weight)
            CHECK(seen & (UINT32_C(1) << id));
}

static void version_eleven_loads_without_completion(void)
{
    JelliSave save = {0}, loaded;
    jelli_game_init(&save.game);
    CHECK(save.game.count == 1u);
    save.game.pets[0].activity_day = 123u;
    save.game.pets[0].completed_moments = UINT32_C(1) << 6u;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size > 14u);
    memmove(bytes + size - 14u, bytes + size - 8u, 8u);
    size -= 6u;
    bytes[4] = 11u;
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (i * 8u));
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(~crc >> (i * 8u));
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].activity_day == 0u && loaded.game.pets[0].completed_moments == 0u);
}

static void local_midnight_expires_completion(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].phase_offset = 0u;
    game.pets[0].ticks = UINT64_C(23) * 36000u;
    game.timezone_minutes = 60;
    CHECK(jelli_activity_day(&game, &game.pets[0]) == 1u);
    game.pets[0].ticks -= 1u;
    CHECK(jelli_activity_day(&game, &game.pets[0]) == 0u);
    game.pets[0].phase_offset = 1u;
    CHECK(jelli_activity_day(&game, &game.pets[0]) == 1u);
    game.wall_known = true;
    game.wall_seconds = UINT64_C(23) * 3600u;
    CHECK(jelli_activity_day(&game, &game.pets[0]) == 1u);
    game.wall_seconds -= 1u;
    CHECK(jelli_activity_day(&game, &game.pets[0]) == 0u);
}

int main(void)
{
    local_midnight_expires_completion();
    version_eleven_loads_without_completion();
    timed_unlocks_and_dessert();
    random_offers_are_stable();
    favorites_match_legacy();
    legacy_moments_unchanged();
    reading_moment();
    potty_cycle();
    puts("PASS: data-driven moments keep legacy effects; reading and the potty cycle work");
    return 0;
}

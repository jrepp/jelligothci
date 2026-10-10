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
            CHECK(game.pets[0].needs[legacy[i].need] ==
                  base.pets[0].needs[legacy[i].need] + legacy[i].gain);
        if (legacy[i].location >= 0)
            CHECK(game.pets[0].location == (uint8_t)legacy[i].location);
    }
    JelliGame game;
    jelli_game_init(&game);
    CHECK(moment(&game, 0u) == JELLI_OK && game.pets[0].activity == JELLI_EATING);
    CHECK(game.pets[0].moment == 0u); /* Breakfast is a meal, not a play moment. */
    /* Former suggestion windows: 05-11 breakfast, 11-15 tea, 15-19 outing, otherwise movie. */
    for (unsigned hour = 0u; hour < 24u; ++hour) {
        unsigned old = hour >= 5u && hour < 11u    ? 0u
                       : hour >= 11u && hour < 15u ? 1u
                       : hour >= 15u && hour < 19u ? 2u
                                                   : 3u;
        CHECK(jelli_moment_suggested(hour) == old);
    }
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

int main(void)
{
    favorites_match_legacy();
    legacy_moments_unchanged();
    reading_moment();
    potty_cycle();
    puts("PASS: data-driven moments keep legacy effects; reading and the potty cycle work");
    return 0;
}

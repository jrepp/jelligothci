#include "jelli/wake.h"
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

static void sleep_for(JelliGame *game, uint32_t ticks)
{
    jelli_game_resume_begin(game, (uint64_t)ticks * 100u);
    while (!jelli_game_resume_step(game))
        CHECK(jelli_game_valid(game));
    CHECK(jelli_game_valid(game));
}

static void nap_and_persistence(void)
{
    JelliSave save = {0}, loaded;
    jelli_game_init(&save.game);
    CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_REST, 1u, 0u}) == JELLI_OK);
    sleep_for(&save.game, jelli_wake_rules.nap_ticks - 1u);
    CHECK(save.game.pets[0].rest_ticks == jelli_wake_rules.nap_ticks - 1u);
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(jelli_game_command(&loaded.game, (JelliCommand){JELLI_CMD_WAKE, 1u, 0u}) == JELLI_OK);
    CHECK(loaded.game.pets[0].wake_mood == JELLI_WAKE_GROGGY && loaded.game.pets[0].bond == 100u);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    jelli_game_advance(&loaded.game, 100u);
    CHECK(jelli_game_command(&loaded.game, (JelliCommand){JELLI_CMD_WAKE, 1u, 0u}) == JELLI_OK);
    CHECK(loaded.game.pets[0].wake_mood == JELLI_WAKE_HAPPY);
    CHECK(loaded.game.pets[0].bond == 100u + jelli_wake_rules.bond_gain);
    CHECK(loaded.game.pets[0].rest_ticks == 0u && !loaded.game.sleep_log.active);
    size = jelli_save_encode(&loaded, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&save, bytes, size));
    CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_WAKE, 1u, 0u}) ==
          JELLI_NOT_READY);
    CHECK(save.game.pets[0].bond == 100u + jelli_wake_rules.bond_gain);
    CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_REST, 1u, 0u}) == JELLI_OK);
    CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_WAKE, 1u, 0u}) == JELLI_OK);
    CHECK(save.game.pets[0].wake_mood == JELLI_WAKE_GROGGY);
    CHECK(save.game.pets[0].bond == 100u + jelli_wake_rules.bond_gain);
}

static void scheduled_sleep(void)
{
    for (unsigned good = 0; good < 2u; ++good) {
        JelliGame game;
        jelli_game_init(&game);
        JelliPet *pet = &game.pets[0];
        pet->asleep = true;
        pet->scheduled_sleep = true;
        pet->phase_offset = 22u * 36000u;
        pet->bond = 999u;
        sleep_for(&game, jelli_wake_rules.sleep_ticks - (good ? 0u : 1u));
        CHECK(pet->asleep);
        CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_WAKE, 1u, 0u}) == JELLI_OK);
        CHECK(pet->wake_mood == (good ? JELLI_WAKE_HAPPY : JELLI_WAKE_GROGGY));
        CHECK(pet->bond == (good ? 1000u : 999u));
        for (unsigned i = 0; i < 4u; ++i)
            jelli_game_advance(&game, 800u);
        CHECK(pet->wake_mood == JELLI_WAKE_NONE);
    }
}

int main(void)
{
    nap_and_persistence();
    scheduled_sleep();
    puts("Wake thresholds, admitted sleep, save/reload, reward deduplication and bond cap "
         "verified.");
    return 0;
}

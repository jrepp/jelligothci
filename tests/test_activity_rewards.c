#include "jelli/activities.h"
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

static void begin(JelliGame *game)
{
    jelli_game_init(game);
    JelliPet *pet = &game->pets[0];
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        pet->needs[i] = 300u;
    pet->hydration = pet->bond = 300u;
    CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_MOMENT, pet->id, 4u}) == JELLI_OK);
}

static void advance(JelliGame *game, unsigned ticks)
{
    for (unsigned i = 0u; i < ticks; ++i)
        jelli_game_advance(game, 100u);
}

static void immediate_spaced_and_exact(void)
{
    JelliGame game;
    begin(&game);
    JelliPet *pet = &game.pets[0];
    unsigned total = pet->moment_rewards[JELLI_AMUSEMENT];
    CHECK(pet->needs[JELLI_AMUSEMENT] > 300u);
    unsigned previous = pet->needs[JELLI_AMUSEMENT], last_tick = 0u, hits = 1u;
    unsigned first_gap = 0u;
    bool changed_gap = false, changed_size = false;
    unsigned first_gain = previous - 300u;
    for (unsigned tick = 1u; tick <= 80u; ++tick) {
        advance(&game, 1u);
        unsigned value = pet->needs[JELLI_AMUSEMENT];
        if (value > previous) {
            unsigned gap = tick - last_tick;
            CHECK(gap >= 8u && gap <= 24u);
            if (!first_gap)
                first_gap = gap;
            if (gap != first_gap)
                changed_gap = true;
            if (value - previous != first_gain)
                changed_size = true;
            last_tick = tick;
            ++hits;
        }
        previous = value;
    }
    CHECK(hits >= 4u && changed_gap && changed_size);
    CHECK(pet->needs[JELLI_AMUSEMENT] == 300u + total);
    CHECK(pet->activity == JELLI_IDLE && !pet->moment_pulse);
    CHECK(jelli_game_valid(&game));
}

static void checkpoint_and_offline(void)
{
    JelliSave save, loaded;
    memset(&save, 0, sizeof(save));
    begin(&save.game);
    advance(&save.game, 23u);
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].moment_pulse == save.game.pets[0].moment_pulse);
    CHECK(memcmp(loaded.game.pets[0].moment_rewards, save.game.pets[0].moment_rewards,
                 sizeof(save.game.pets[0].moment_rewards)) == 0);
    advance(&save.game, 57u);
    advance(&loaded.game, 57u);
    CHECK(memcmp(save.game.pets[0].needs, loaded.game.pets[0].needs,
                 sizeof(save.game.pets[0].needs)) == 0);
    begin(&save.game);
    loaded.game = save.game;
    advance(&save.game, 80u);
    jelli_game_resume_begin(&loaded.game, 8000u);
    while (loaded.game.resuming)
        jelli_game_resume_step(&loaded.game);
    CHECK(memcmp(save.game.pets[0].needs, loaded.game.pets[0].needs,
                 sizeof(save.game.pets[0].needs)) == 0);
}

static void cancelled_food_is_not_free(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].needs[JELLI_SATIETY] = 200u;
    unsigned food = game.food;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, 1u, 0u}) == JELLI_OK);
    CHECK(game.food == food - 1u && game.pets[0].needs[JELLI_SATIETY] > 200u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_CARE, 1u, 0u}) == JELLI_OK);
    CHECK(!game.pets[0].moment_pulse && jelli_game_valid(&game));
    advance(&game, 300u);
    CHECK(game.food == food - 1u && !game.pets[0].reward_pending);
}

static void malformed_checkpoint_rejected(void)
{
    JelliGame game;
    begin(&game);
    game.pets[0].moment_pulse = 127u;
    CHECK(!jelli_game_valid(&game));
    begin(&game);
    game.pets[0].moment_rewards[0] = UINT16_MAX;
    CHECK(!jelli_game_valid(&game));
}

int main(void)
{
    immediate_spaced_and_exact();
    checkpoint_and_offline();
    cancelled_food_is_not_free();
    malformed_checkpoint_rejected();
    puts("PASS: immediate jittered rewards preserve totals, checkpoint and offline progress");
    return 0;
}

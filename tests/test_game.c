#include "jelli/game.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static JelliResult command(JelliGame *game, JelliCommandKind kind, uint32_t actor, uint32_t value)
{
    return jelli_game_command(game,
                              (JelliCommand){.kind = kind, .actor_id = actor, .value = value});
}

static void advance_ms(JelliGame *game, uint64_t milliseconds)
{
    while (milliseconds > 0u) {
        uint64_t step = (milliseconds > 800u) ? 800u : milliseconds;
        jelli_game_advance(game, step);
        milliseconds -= step;
    }
}

static void initialization_and_validation(void)
{
    JelliGame game;
    jelli_game_init(&game);
    CHECK(jelli_game_valid(&game));
    CHECK(game.count == 2u && game.active == 0u && game.food == 5u && game.gifts == 3u);
    CHECK(game.pets[0].id == 1u && game.pets[1].id == 2u);
    jelli_game_advance(&game, 100u);
    CHECK(!game.pets[0].asleep);
    CHECK(command(&game, JELLI_CMD_FEED, 2u, 0u) == JELLI_INVALID_TARGET);
    game.pets[0].needs[JELLI_ENERGY] = 1001u;
    CHECK(!jelli_game_valid(&game));
    game.pets[0].needs[JELLI_ENERGY] = 700u;
    game.pets[0].need_remainders[JELLI_HYGIENE] = 600u;
    CHECK(!jelli_game_valid(&game));
    game.pets[0].need_remainders[JELLI_HYGIENE] = 0u;
    game.pets[1].id = game.pets[0].id;
    CHECK(!jelli_game_valid(&game));
}

static void feed_reward_and_full_claim(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.food = JELLI_STACK_LIMIT;
    CHECK(command(&game, JELLI_CMD_FEED, 1u, 0u) == JELLI_OK);
    CHECK(game.food == JELLI_STACK_LIMIT);
    advance_ms(&game, 4900u);
    CHECK(game.food == JELLI_STACK_LIMIT && !game.pets[0].reward_pending);
    advance_ms(&game, 100u);
    CHECK(game.food == JELLI_STACK_LIMIT - 1u && game.pets[0].reward_pending);
    CHECK(command(&game, JELLI_CMD_CLAIM, 1u, 0u) == JELLI_FULL);
    CHECK(game.pets[0].reward_pending && !game.pets[0].reward_claimed);
    game.food = 17u;
    CHECK(command(&game, JELLI_CMD_CLAIM, 1u, 0u) == JELLI_OK);
    CHECK(game.food == 20u && !game.pets[0].reward_pending && game.pets[0].reward_claimed);
    CHECK(command(&game, JELLI_CMD_CLAIM, 1u, 0u) == JELLI_NOT_READY);
    CHECK(game.food == 20u);
    CHECK(jelli_game_valid(&game));
    game.pets[0].needs[JELLI_SATIETY] = 400u;
    JelliResult repeated_feed = command(&game, JELLI_CMD_FEED, 1u, 0u);
    CHECK(repeated_feed == JELLI_OK);
    advance_ms(&game, 5000u);
    CHECK(game.pets[0].reward_claimed && !game.pets[0].reward_pending);
    CHECK(jelli_game_valid(&game));
}

static void other_care_effects(void)
{
    JelliGame game;
    jelli_game_init(&game);
    uint16_t amusement = game.pets[0].needs[JELLI_AMUSEMENT];
    CHECK(command(&game, JELLI_CMD_PLAY, 1u, 0u) == JELLI_OK);
    advance_ms(&game, 8000u);
    CHECK(game.pets[0].needs[JELLI_AMUSEMENT] == amusement + 250u);
    CHECK(command(&game, JELLI_CMD_CLEAN, 1u, 0u) == JELLI_OK);
    advance_ms(&game, 5000u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 1000u);
    uint16_t bond = game.pets[0].bond;
    CHECK(command(&game, JELLI_CMD_GIFT, 1u, 0u) == JELLI_OK);
    CHECK(game.gifts == 3u);
    advance_ms(&game, 5000u);
    CHECK(game.gifts == 2u && game.pets[0].bond == bond + 25u);
    CHECK(jelli_game_valid(&game));
}

static void zero_inventory_recovery(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.food = 0u;
    game.gifts = 0u;
    for (size_t i = 0u; i < JELLI_NEED_COUNT; ++i)
        game.pets[0].needs[i] = 0u;
    game.pets[0].health = JELLI_UNWELL;
    CHECK(jelli_game_valid(&game));
    CHECK(command(&game, JELLI_CMD_FEED, 1u, 0u) == JELLI_NO_ITEM);
    CHECK(command(&game, JELLI_CMD_CARE, 1u, 0u) == JELLI_OK);
    CHECK(game.pets[0].health == JELLI_RECOVERING);
    CHECK(command(&game, JELLI_CMD_CARE, 1u, 0u) == JELLI_BUSY);
    advance_ms(&game, 29900u);
    CHECK(game.pets[0].health == JELLI_RECOVERING);
    advance_ms(&game, 100u);
    CHECK(game.pets[0].health == JELLI_WELL);
    for (size_t i = 0u; i < JELLI_NEED_COUNT; ++i)
        CHECK(game.pets[0].needs[i] >= 400u);
    CHECK(game.pets[0].health == JELLI_WELL);
    CHECK(game.food == 0u && game.gifts == 0u);
}

static void care_cancels_and_wakes_without_cost(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].needs[JELLI_SATIETY] = 100u;
    CHECK(command(&game, JELLI_CMD_FEED, 1u, 0u) == JELLI_OK);
    CHECK(command(&game, JELLI_CMD_CARE, 1u, 0u) == JELLI_OK);
    CHECK(game.pets[0].activity == JELLI_CARING && game.pets[0].health == JELLI_RECOVERING);
    CHECK(game.food == 5u && !game.pets[0].asleep);
    advance_ms(&game, 30000u);
    CHECK(game.food == 5u && game.pets[0].health == JELLI_WELL);
    CHECK(!game.pets[0].reward_pending);
    CHECK(command(&game, JELLI_CMD_REST, 1u, 0u) == JELLI_OK);
    CHECK(game.pets[0].asleep);
    CHECK(command(&game, JELLI_CMD_CARE, 1u, 0u) == JELLI_OK);
    CHECK(!game.pets[0].asleep && game.pets[0].health == JELLI_RECOVERING);
    CHECK(jelli_game_valid(&game));
}

static void rate_remainders_and_sleep_transition(void)
{
    JelliGame game;
    jelli_game_init(&game);
    advance_ms(&game, 30000u);
    CHECK(game.pets[0].needs[JELLI_SATIETY] == 496u);
    CHECK(game.pets[0].needs[JELLI_ENERGY] == 698u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 699u);
    CHECK(game.pets[0].need_remainders[JELLI_HYGIENE] == 300u);
    CHECK(command(&game, JELLI_CMD_REST, 1u, 0u) == JELLI_OK);
    advance_ms(&game, 30000u);
    CHECK(game.pets[0].needs[JELLI_SATIETY] == 494u);
    CHECK(game.pets[0].needs[JELLI_ENERGY] == 703u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 698u);
    CHECK(game.pets[0].need_remainders[JELLI_HYGIENE] == 300u);
}

static void energy_remainder_survives_direction_change(void)
{
    JelliGame game;
    jelli_game_init(&game);
    advance_ms(&game, 29900u);
    CHECK(game.pets[0].needs[JELLI_ENERGY] == 699u);
    CHECK(game.pets[0].need_remainders[JELLI_ENERGY] == 596u);
    CHECK(command(&game, JELLI_CMD_REST, 1u, 0u) == JELLI_OK);
    advance_ms(&game, 100u);
    CHECK(game.pets[0].needs[JELLI_ENERGY] == 699u);
    CHECK(game.pets[0].need_remainders[JELLI_ENERGY] == 586u);
    CHECK(jelli_game_valid(&game));
}

static void neglect_survives_nap_and_wake_grace(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].needs[JELLI_SATIETY] = 100u;
    game.pets[0].hunger_low = true;
    game.pets[0].hunger_counted = true;
    game.pets[0].hunger_due = 0u;
    game.pets[0].needs[JELLI_ENERGY] = 150u;
    CHECK(jelli_game_valid(&game));
    advance_ms(&game, 100u);
    CHECK(game.pets[0].asleep && !game.pets[0].scheduled_sleep);
    game.pets[0].needs[JELLI_ENERGY] = 800u;
    advance_ms(&game, 100u);
    CHECK(!game.pets[0].asleep);
    CHECK(game.pets[0].hunger_low && game.pets[0].hunger_counted);
    advance_ms(&game, 6000u);
    CHECK(game.pets[0].neglect == 0u && game.pets[0].hunger_counted);
    game.pets[0].hunger_counted = false;
    game.pets[0].hunger_due = game.pets[0].ticks + 10u;
    game.pets[0].asleep = true;
    game.pets[0].nap_due = game.pets[0].ticks + 20u;
    CHECK(jelli_game_valid(&game));
    game.pets[0].needs[JELLI_ENERGY] = 800u;
    advance_ms(&game, 100u);
    CHECK(!game.pets[0].asleep);
    CHECK(game.pets[0].hunger_due > game.pets[0].ticks);
}

static void bedtime_blocks_optional_actions(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].ticks = UINT64_C(13) * 36000u;
    CHECK(jelli_game_valid(&game));
    CHECK(command(&game, JELLI_CMD_FEED, 1u, 0u) == JELLI_BUSY);
    CHECK(command(&game, JELLI_CMD_PLAY, 1u, 0u) == JELLI_BUSY);
    CHECK(command(&game, JELLI_CMD_GIFT, 1u, 0u) == JELLI_BUSY);
}

static void manual_wake_overrides_bedtime_window(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].ticks = UINT64_C(13) * 36000u;
    CHECK(command(&game, JELLI_CMD_REST, 1u, 0u) == JELLI_OK);
    CHECK(game.pets[0].asleep && !game.pets[0].scheduled_sleep);
    CHECK(command(&game, JELLI_CMD_WAKE, 1u, 0u) == JELLI_OK);
    advance_ms(&game, 3600000u);
    CHECK(!game.pets[0].asleep);
}

static void clock_saturation_is_safe(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].ticks = UINT64_MAX;
    game.pets[0].phase_offset =
        (uint32_t)((JELLI_DAY_TICKS + UINT32_C(324000) - (uint32_t)(UINT64_MAX % JELLI_DAY_TICKS)) %
                   JELLI_DAY_TICKS);
    game.pets[0].form = 1u;
    CHECK(jelli_game_valid(&game));
    CHECK(command(&game, JELLI_CMD_FEED, 1u, 0u) == JELLI_NOT_READY);
    CHECK(command(&game, JELLI_CMD_CARE, 1u, 0u) == JELLI_NOT_READY);
    CHECK(command(&game, JELLI_CMD_REST, 1u, 0u) == JELLI_NOT_READY);
    advance_ms(&game, 100u);
    CHECK(game.pets[0].ticks == UINT64_MAX);
    CHECK(jelli_game_valid(&game));
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void randomized_valid_commands_and_time(void)
{
    JelliGame game;
    uint32_t random_state = UINT32_C(0x51a7c0de);
    jelli_game_init(&game);
    for (uint16_t i = 0u; i < 500u; ++i) {
        uint32_t value = next_random(&random_state);
        uint32_t command_value = value;
        JelliCommandKind kind = (JelliCommandKind)(value % 11u);
        if (kind == JELLI_CMD_ACTIVATE)
            command_value = UINT32_C(1) + next_random(&random_state) % 2u;
        (void)command(&game, kind, game.pets[game.active].id, command_value);
        CHECK(jelli_game_valid(&game));
        if ((next_random(&random_state) % 13u) == 0u) {
            jelli_game_resume_begin(&game, next_random(&random_state) % 180000u);
            CHECK(jelli_game_valid(&game));
            while (!jelli_game_resume_step(&game))
                CHECK(jelli_game_valid(&game));
        } else {
            jelli_game_advance(&game, next_random(&random_state) % 1001u);
        }
        CHECK(jelli_game_valid(&game));
    }
}

static void sleep_and_stored_freeze(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].ticks = UINT64_C(13) * 36000u - 1u;
    game.pets[0].awake_until = 0u;
    advance_ms(&game, 100u);
    CHECK(game.pets[0].asleep && game.pets[0].scheduled_sleep);
    CHECK(command(&game, JELLI_CMD_FEED, 1u, 0u) == JELLI_ASLEEP);
    CHECK(command(&game, JELLI_CMD_WAKE, 1u, 0u) == JELLI_OK);
    CHECK(!game.pets[0].asleep);
    advance_ms(&game, 3600000u);
    CHECK(!game.pets[0].asleep);
    CHECK(command(&game, JELLI_CMD_ACTIVATE, 1u, 2u) == JELLI_OK);
    uint64_t frozen_ticks = game.pets[0].ticks;
    uint16_t frozen_energy = game.pets[0].needs[JELLI_ENERGY];
    advance_ms(&game, 7200000u);
    CHECK(game.pets[0].ticks == frozen_ticks);
    CHECK(game.pets[0].needs[JELLI_ENERGY] == frozen_energy);
    CHECK(command(&game, JELLI_CMD_ACTIVATE, 2u, 1u) == JELLI_OK);
    CHECK(game.pets[0].ticks == frozen_ticks);
}

static void evolution_and_live_bounds(void)
{
    JelliGame fast;
    JelliGame slow;
    jelli_game_init(&fast);
    slow = fast;
    advance_ms(&fast, 60000u);
    for (uint16_t i = 0u; i < 600u; ++i)
        jelli_game_advance(&slow, 100u);
    CHECK(fast.pets[0].form == 1u && slow.pets[0].form == 1u);
    CHECK(fast.pets[0].ticks == slow.pets[0].ticks);
    CHECK(fast.pets[0].needs[JELLI_SATIETY] == slow.pets[0].needs[JELLI_SATIETY]);
    jelli_game_advance(&fast, 10000u);
    CHECK(fast.backlog_ms <= 2000u && fast.discarded_ms >= 8000u);
}

static void capped_resume_and_command_lock(void)
{
    JelliGame game;
    jelli_game_init(&game);
    uint64_t original = game.pets[0].ticks;
    jelli_game_resume_begin(&game, JELLI_OFFLINE_CAP_MS + 1000u);
    CHECK(game.resuming && game.resume_remaining_ms == JELLI_OFFLINE_CAP_MS);
    CHECK(game.discarded_ms == 1000u);
    CHECK(command(&game, JELLI_CMD_FEED, 1u, 0u) == JELLI_BUSY);
    uint8_t calls = 0u;
    while (!jelli_game_resume_step(&game)) {
        CHECK(jelli_game_valid(&game));
        ++calls;
        CHECK(calls < 45u);
    }
    CHECK(jelli_game_valid(&game));
    CHECK(!game.resuming && game.resume_remaining_ms == 0u);
    CHECK(game.pets[0].ticks == original + JELLI_OFFLINE_CAP_MS / 100u);
    CHECK(game.pets[1].ticks == 0u && calls == 44u);
}

int main(void)
{
    initialization_and_validation();
    feed_reward_and_full_claim();
    other_care_effects();
    zero_inventory_recovery();
    care_cancels_and_wakes_without_cost();
    rate_remainders_and_sleep_transition();
    energy_remainder_survives_direction_change();
    neglect_survives_nap_and_wake_grace();
    bedtime_blocks_optional_actions();
    manual_wake_overrides_bedtime_window();
    clock_saturation_is_safe();
    randomized_valid_commands_and_time();
    sleep_and_stored_freeze();
    evolution_and_live_bounds();
    capped_resume_and_command_lock();
    puts("PASS: bounded care, rewards, sleep, collection, evolution and resume");
    return 0;
}

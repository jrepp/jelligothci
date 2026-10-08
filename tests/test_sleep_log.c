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

static void known_and_unknown_sessions(void)
{
    JelliSleepLog log = {0};
    CHECK(jelli_sleep_log_valid(&log));
    CHECK(jelli_sleep_log_begin(&log, 1u, 17u, true, 1000u));
    CHECK(!jelli_sleep_log_begin(&log, 1u, 18u, true, 1001u));
    CHECK(jelli_sleep_log_finish(&log, 37u, true, 29800u));
    CHECK(log.sessions[0].duration_seconds == 28800u);
    CHECK(log.sessions[0].flags == 7u && log.total_seconds == 28800u);
    CHECK(jelli_sleep_log_begin(&log, 1u, 100u, false, 999999u));
    CHECK(jelli_sleep_log_finish(&log, 36123u, false, 999999u));
    CHECK(log.sessions[1].duration_seconds == 3602u);
    CHECK(log.sessions[1].flags == 0u);
    CHECK(log.sessions[1].bed_unix_seconds == 0u && log.sessions[1].wake_unix_seconds == 0u);
    CHECK(log.total_seconds == 32402u && jelli_sleep_log_valid(&log));
    CHECK(!jelli_sleep_log_finish(&log, 50000u, true, 30000u));
    CHECK(jelli_sleep_log_begin(&log, 1u, 50000u, true, 30000u));
    CHECK(jelli_sleep_log_finish(&log, 50020u, true, 20000u));
    CHECK(log.sessions[2].flags == 3u && log.sessions[2].duration_seconds == 2u);
    CHECK(jelli_sleep_log_valid(&log)); /* Clock rewind uses actual simulated duration. */
}

static void history_bounds_and_saturation(void)
{
    JelliSleepLog log = {0};
    for (unsigned i = 0u; i < 20u; ++i) {
        CHECK(jelli_sleep_log_begin(&log, 1u, (uint64_t)i * 10u, false, 0u));
        CHECK(jelli_sleep_log_finish(&log, (uint64_t)(i + 1u) * 10u, false, 0u));
    }
    CHECK(log.count == 8u && log.head == 4u && log.total_seconds == 20u);
    CHECK(log.sessions[3].start_tick == 190u && jelli_sleep_log_valid(&log));
    log.total_seconds = UINT64_MAX - 1u;
    CHECK(jelli_sleep_log_begin(&log, 1u, 0u, true, 0u));
    CHECK(jelli_sleep_log_finish(&log, 0u, true, UINT64_MAX));
    CHECK(log.total_seconds == UINT64_MAX && log.sessions[4].duration_seconds == UINT32_MAX);
    CHECK(jelli_sleep_log_valid(&log));
    log.sessions[4].flags = 16u;
    CHECK(!jelli_sleep_log_valid(&log));
}

static void linked_rest_until_manual_wake(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.wall_known = true;
    game.wall_seconds = 1700000000u;
    const JelliPet *pet = &game.pets[0];
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, pet->id, 0u}) == JELLI_OK);
    CHECK(game.sleep_log.active && game.sleep_log.pet_id == pet->id);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, pet->id, 2u}) ==
          JELLI_NOT_READY);
    jelli_game_resume_begin(&game, 8u * UINT64_C(3600000));
    while (!jelli_game_resume_step(&game))
        CHECK(jelli_game_valid(&game));
    CHECK(pet->asleep && pet->needs[JELLI_ENERGY] == 1000u && game.sleep_log.active);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_CARE, pet->id, 0u}) == JELLI_OK);
    CHECK(!pet->asleep && game.sleep_log.active);
    for (unsigned i = 0u; i < 300u; ++i)
        jelli_game_advance(&game, 100u);
    CHECK(pet->asleep && game.sleep_log.active && pet->activity == JELLI_IDLE);
    game.wall_seconds += UINT64_C(8) * 3600u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_WAKE, pet->id, 0u}) == JELLI_OK);
    CHECK(!pet->asleep && !game.sleep_log.active);
    CHECK(game.sleep_log.sessions[0].duration_seconds == 28800u);
    CHECK(game.sleep_log.sessions[0].flags == 15u && jelli_game_valid(&game));
    CHECK(game.sleep_log.sessions[0].bed_energy == 700u);
    CHECK(game.sleep_log.sessions[0].bed_sleep_score == 750u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, pet->id, 2u}) == JELLI_OK);
}

int main(void)
{
    known_and_unknown_sessions();
    history_bounds_and_saturation();
    linked_rest_until_manual_wake();
    puts("PASS: explicit linked sleep journal, unknown timestamps, bounds and manual wake");
    return 0;
}

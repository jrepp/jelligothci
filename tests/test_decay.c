#include "jelli/game.h"
#include "jelli/save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void check_same_checkpoint(const JelliGame *a, const JelliGame *b)
{
    static uint8_t first[JELLI_SAVE_CAPACITY], second[JELLI_SAVE_CAPACITY];
    JelliSave saved_a = {.game = *a}, saved_b = {.game = *b};
    size_t size = jelli_save_encode(&saved_a, first, sizeof(first));
    CHECK(size > 0u && jelli_save_encode(&saved_b, second, sizeof(second)) == size);
    CHECK(memcmp(first, second, size) == 0);
}

static void advance_live(JelliGame *game, uint64_t milliseconds)
{
    while (milliseconds > 0u) {
        uint64_t step = milliseconds > 800u ? 800u : milliseconds;
        jelli_game_advance(game, step);
        milliseconds -= step;
    }
}

static void resume(JelliGame *game, uint64_t milliseconds)
{
    jelli_game_resume_begin(game, milliseconds);
    while (!jelli_game_resume_step(game))
        CHECK(jelli_game_valid(game));
}

static JelliResult command(JelliGame *game, JelliCommandKind kind)
{
    return jelli_game_command(game, (JelliCommand){kind, game->pets[game->active].id, 0u});
}

static void gradual_hour_and_night(void)
{
    JelliGame game;
    jelli_game_init(&game);
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        game.pets[0].needs[i] = 900u;
    resume(&game, 3600000u);
    CHECK(game.pets[0].needs[JELLI_SATIETY] == 840u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 885u);
    CHECK(command(&game, JELLI_CMD_REST) == JELLI_OK);
    resume(&game, 8u * UINT64_C(3600000));
    CHECK(game.pets[0].asleep);
    CHECK(game.pets[0].needs[JELLI_SATIETY] == 720u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 825u);
}

static void mess_only_on_completed_actions(void)
{
    JelliGame game;
    jelli_game_init(&game);
    CHECK(command(&game, JELLI_CMD_FEED) == JELLI_OK);
    CHECK(command(&game, JELLI_CMD_PLAY) == JELLI_BUSY);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 700u);
    advance_live(&game, 4900u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 700u);
    advance_live(&game, 100u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 690u);
    CHECK(command(&game, JELLI_CMD_PLAY) == JELLI_OK);
    advance_live(&game, 7900u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 690u);
    advance_live(&game, 100u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 670u);
    CHECK(command(&game, JELLI_CMD_FEED) == JELLI_OK);
    CHECK(command(&game, JELLI_CMD_CARE) == JELLI_OK);
    advance_live(&game, 30000u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 670u);
    CHECK(game.pets[0].habits.lifetime_meals == 1u);
    game.food = 0u;
    CHECK(command(&game, JELLI_CMD_FEED) == JELLI_NO_ITEM);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 670u);
}

static void unaligned_completion_and_sleep_match(void)
{
    JelliGame live;
    jelli_game_init(&live);
    advance_live(&live, 700u);
    CHECK(command(&live, JELLI_CMD_FEED) == JELLI_OK);
    JelliGame offline = live;
    advance_live(&live, 71713u);
    resume(&offline, 71713u);
    check_same_checkpoint(&live, &offline);
    CHECK(command(&live, JELLI_CMD_PLAY) == JELLI_OK);
    offline = live;
    advance_live(&live, 63119u);
    resume(&offline, 63119u);
    check_same_checkpoint(&live, &offline);
    CHECK(command(&live, JELLI_CMD_REST) == JELLI_OK);
    offline = live;
    advance_live(&live, 79127u);
    resume(&offline, 79127u);
    check_same_checkpoint(&live, &offline);
    CHECK(live.pets[0].need_remainders[JELLI_HYGIENE] < 600u);
}

static void mess_clamps_at_zero(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].needs[JELLI_HYGIENE] = 5u;
    CHECK(command(&game, JELLI_CMD_PLAY) == JELLI_OK);
    advance_live(&game, 8000u);
    CHECK(game.pets[0].needs[JELLI_HYGIENE] == 0u);
    CHECK(game.pets[0].need_remainders[JELLI_HYGIENE] == 0u);
    CHECK(jelli_game_valid(&game));
}

int main(void)
{
    gradual_hour_and_night();
    mess_only_on_completed_actions();
    unaligned_completion_and_sleep_match();
    mess_clamps_at_zero();
    puts("PASS: gradual needs, completed-activity mess, clamps and live/offline consistency");
    return 0;
}

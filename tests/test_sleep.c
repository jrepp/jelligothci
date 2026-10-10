#include "jelli/wake.h"
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

static void resume(JelliGame *game, uint64_t milliseconds)
{
    jelli_game_resume_begin(game, milliseconds);
    while (!jelli_game_resume_step(game))
        CHECK(jelli_game_valid(game));
    CHECK(jelli_game_valid(game));
}

static void start_night(JelliGame *game)
{
    jelli_game_init(game);
    JelliPet *pet = &game->pets[0];
    pet->ticks = UINT64_C(13) * 36000u;
    pet->form = 1u;
    pet->asleep = true;
    pet->scheduled_sleep = true;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        pet->needs[i] = 900u;
    pet->needs[JELLI_ENERGY] = 100u;
    pet->health = JELLI_UNWELL;
    CHECK(jelli_game_valid(game));
}

static void night_restores_without_feeding(void)
{
    JelliGame game;
    start_night(&game);
    resume(&game, UINT64_C(6) * 3600000u);
    resume(&game, UINT64_C(2) * 3600000u);
    const JelliPet *pet = &game.pets[0];
    CHECK(!pet->asleep && pet->health == JELLI_WELL);
    CHECK(pet->needs[JELLI_ENERGY] == 1000u);
    CHECK(pet->needs[JELLI_SATIETY] == 780u);
    CHECK(pet->needs[JELLI_HYGIENE] == 840u);
    CHECK(pet->needs[JELLI_AMUSEMENT] == 900u);
    CHECK(pet->needs[JELLI_SOCIAL] == 900u);
    CHECK(game.food == 5u && game.gifts == 3u && pet->bond == 100u + jelli_wake_rules.bond_gain);
}

static void unresolved_needs_prevent_sleep_healing(void)
{
    for (unsigned need = 0u; need < JELLI_NEED_COUNT; ++need) {
        if (need == JELLI_ENERGY)
            continue;
        JelliGame game;
        start_night(&game);
        game.pets[0].needs[need] = 100u;
        resume(&game, 3600000u);
        CHECK(game.pets[0].needs[JELLI_ENERGY] == 700u);
        CHECK(game.pets[0].health == JELLI_UNWELL);
        CHECK(game.pets[0].needs[need] <= 100u);
    }
}

static void live_and_offline_sleep_match(void)
{
    JelliGame live;
    start_night(&live);
    live.pets[0].needs[JELLI_ENERGY] = 395u;
    live.pets[0].need_remainders[JELLI_AMUSEMENT] = 217u;
    live.pets[0].need_remainders[JELLI_SOCIAL] = 599u;
    JelliGame offline = live;
    for (unsigned i = 0u; i < 7500u; ++i)
        jelli_game_advance(&live, 800u);
    resume(&offline, 6000000u);
    check_same_checkpoint(&live, &offline);
    CHECK(live.pets[0].health == JELLI_WELL);
    CHECK(live.pets[0].need_remainders[JELLI_AMUSEMENT] == 217u);
    CHECK(live.pets[0].need_remainders[JELLI_SOCIAL] == 599u);
}

static void advance_live(JelliGame *game, uint64_t milliseconds)
{
    while (milliseconds > 0u) {
        uint64_t step = milliseconds > 800u ? 800u : milliseconds;
        jelli_game_advance(game, step);
        milliseconds -= step;
    }
}

static void unaligned_nap_and_activity_match(void)
{
    JelliGame live;
    jelli_game_init(&live);
    JelliPet *pet = &live.pets[0];
    pet->needs[JELLI_ENERGY] = 149u;
    jelli_game_advance(&live, 100u); /* Automatic nap has no linked human diary. */
    CHECK(pet->asleep && !live.sleep_log.active);
    pet->needs[JELLI_ENERGY] = 797u;
    pet->need_remainders[JELLI_ENERGY] = 37u;
    JelliGame offline = live;
    advance_live(&live, 61100u);
    resume(&offline, 61100u);
    CHECK(!live.pets[0].asleep);
    check_same_checkpoint(&live, &offline);
    CHECK(jelli_game_command(&live, (JelliCommand){JELLI_CMD_PLAY, pet->id, 0u}) == JELLI_OK);
    offline = live;
    advance_live(&live, 91300u);
    resume(&offline, 91300u);
    check_same_checkpoint(&live, &offline);
    CHECK(live.pets[0].habits.lifetime_play_ticks == 80u);
}

static void set_sleep_history(JelliPet *pet, uint64_t slept)
{
    pet->habits = (JelliHabits){0};
    jelli_habits_advance(&pet->habits, 0u, JELLI_HABIT_DAY_TICKS, false, false);
    jelli_habits_advance(&pet->habits, pet->habits.cursor_ticks, slept, true, false);
    pet->ticks = pet->habits.cursor_ticks;
    pet->form = 1u;
}

static void sleep_modifiers_and_meals(void)
{
    JelliGame game;
    jelli_game_init(&game);
    JelliPet *pet = &game.pets[0];
    set_sleep_history(pet, 0u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_PLAY, pet->id, 0u}) == JELLI_OK);
    advance_live(&game, 8000u);
    CHECK(pet->needs[JELLI_AMUSEMENT] == 700u); /* Low sleep: +200, not +250. */
    uint16_t energy = pet->needs[JELLI_ENERGY];
    advance_live(&game, 60000u);
    CHECK(pet->needs[JELLI_ENERGY] == energy - 5u);
    set_sleep_history(pet, 8u * JELLI_HABIT_HOUR_TICKS);
    pet->needs[JELLI_AMUSEMENT] = 400u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_PLAY, pet->id, 0u}) == JELLI_OK);
    advance_live(&game, 8000u);
    CHECK(pet->needs[JELLI_AMUSEMENT] == 700u); /* High sleep: +300 once. */
    pet->needs[JELLI_SOCIAL] = 500u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_HEALTH, pet->id, 1u}) == JELLI_OK);
    CHECK(pet->needs[JELLI_SOCIAL] == 512u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, pet->id, 0u}) == JELLI_OK);
    CHECK(pet->habits.lifetime_meals == 0u);
    advance_live(&game, 4900u);
    CHECK(pet->habits.lifetime_meals == 0u);
    advance_live(&game, 100u);
    CHECK(pet->habits.lifetime_meals == 1u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, pet->id, 0u}) == JELLI_OK);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_CARE, pet->id, 0u}) == JELLI_OK);
    advance_live(&game, 30000u);
    CHECK(pet->habits.lifetime_meals == 1u);
}

static void unaligned_sleep_threshold_match(void)
{
    JelliGame live;
    jelli_game_init(&live);
    JelliPet *pet = &live.pets[0];
    set_sleep_history(pet, 2u * JELLI_HABIT_HOUR_TICKS);
    CHECK(jelli_habits_sleep_score(&pet->habits) == 250u);
    /* An oldest full sleeping hour begins fading within this minute. */
    pet->habits.bins[(pet->habits.head + 1u) % JELLI_HABIT_BIN_COUNT].sleep_ticks = 36000u;
    pet->habits.bins[(pet->habits.head + 24u) % JELLI_HABIT_BIN_COUNT].sleep_ticks = 0u;
    pet->ticks += 113u;
    jelli_habits_advance(&pet->habits, pet->habits.cursor_ticks, 113u, false, false);
    JelliGame offline = live;
    advance_live(&live, 71900u);
    resume(&offline, 71900u);
    check_same_checkpoint(&live, &offline);
    CHECK(jelli_habits_sleep_score(&live.pets[0].habits) < 250u);
}

int main(void)
{
    night_restores_without_feeding();
    unresolved_needs_prevent_sleep_healing();
    live_and_offline_sleep_match();
    unaligned_nap_and_activity_match();
    sleep_modifiers_and_meals();
    unaligned_sleep_threshold_match();
    puts("PASS: restorative sleep, unresolved care needs and live/offline consistency");
    return 0;
}

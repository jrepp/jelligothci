#include "jelli/habits.h"

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

static void provisional_prior_and_targets(void)
{
    JelliHabits h = {0};
    CHECK(jelli_habits_valid(&h));
    CHECK(jelli_habits_sleep_score(&h) == 750u);
    CHECK(jelli_habits_food_score(&h) == 750u);
    CHECK(jelli_habits_play_score(&h) == 750u);
    CHECK(jelli_habits_totals(&h).coverage_ticks == 0u);
    jelli_habits_advance(&h, 0u, JELLI_HABIT_DAY_TICKS, false, false);
    CHECK(jelli_habits_sleep_score(&h) == 0u);
    CHECK(jelli_habits_food_score(&h) == 0u);
    CHECK(jelli_habits_play_score(&h) == 0u);
    jelli_habits_advance(&h, h.cursor_ticks, 8u * JELLI_HABIT_HOUR_TICKS, true, false);
    CHECK(jelli_habits_sleep_score(&h) == 1000u);
    jelli_habits_advance(&h, h.cursor_ticks, 6000u, false, true);
    CHECK(jelli_habits_play_score(&h) == 1000u);
    for (unsigned i = 0u; i < 3u; ++i)
        jelli_habits_record_meal(&h, h.cursor_ticks);
    CHECK(jelli_habits_food_score(&h) == 1000u);
    CHECK(h.lifetime_meals == 3u && h.lifetime_play_ticks == 6000u);
    CHECK(jelli_habits_valid(&h));
}

static void oldest_hour_fades(void)
{
    JelliHabits h = {0};
    jelli_habits_advance(&h, 0u, JELLI_HABIT_HOUR_TICKS, true, false);
    jelli_habits_record_meal(&h, h.cursor_ticks);
    jelli_habits_advance(&h, h.cursor_ticks, 23u * JELLI_HABIT_HOUR_TICKS, false, false);
    CHECK(jelli_habits_totals(&h).sleep_ticks == JELLI_HABIT_HOUR_TICKS);
    jelli_habits_advance(&h, h.cursor_ticks, JELLI_HABIT_HOUR_TICKS / 2u, false, false);
    CHECK(jelli_habits_totals(&h).sleep_ticks == JELLI_HABIT_HOUR_TICKS / 2u);
    jelli_habits_advance(&h, h.cursor_ticks, JELLI_HABIT_HOUR_TICKS / 2u, false, false);
    CHECK(jelli_habits_totals(&h).sleep_ticks == 0u);
    CHECK(jelli_habits_totals(&h).meals_q16 == 65536u);
    jelli_habits_advance(&h, h.cursor_ticks, JELLI_HABIT_HOUR_TICKS / 2u, false, false);
    CHECK(jelli_habits_totals(&h).meals_q16 == 32768u);
    CHECK(h.lifetime_sleep_ticks == JELLI_HABIT_HOUR_TICKS && h.lifetime_meals == 1u);
}

static void chunking_and_huge_elapsed(void)
{
    JelliHabits a = {0}, b = {0};
    uint64_t start = 17981u;
    jelli_habits_advance(&a, start, 100007u, true, false);
    for (unsigned i = 0u; i < 100007u; ++i)
        jelli_habits_advance(&b, start + i, 1u, true, false);
    CHECK(a.head == b.head && a.cursor_ticks == b.cursor_ticks);
    CHECK(a.observed_ticks == b.observed_ticks && a.lifetime_sleep_ticks == b.lifetime_sleep_ticks);
    CHECK(a.lifetime_play_ticks == b.lifetime_play_ticks && a.lifetime_meals == b.lifetime_meals);
    for (unsigned i = 0u; i < JELLI_HABIT_BIN_COUNT; ++i) {
        CHECK(a.bins[i].sleep_ticks == b.bins[i].sleep_ticks);
        CHECK(a.bins[i].play_ticks == b.bins[i].play_ticks);
        CHECK(a.bins[i].meals == b.bins[i].meals);
    }
    CHECK(jelli_habits_valid(&a));
    JelliHabits huge = {0};
    jelli_habits_advance(&huge, 0u, UINT64_MAX, false, true);
    CHECK(jelli_habits_valid(&huge));
    CHECK(huge.observed_ticks == UINT64_MAX && huge.lifetime_play_ticks == UINT64_MAX);
    CHECK(jelli_habits_totals(&huge).play_ticks == JELLI_HABIT_DAY_TICKS);
    jelli_habits_advance(&huge, UINT64_MAX, UINT64_MAX, false, true);
    CHECK(huge.observed_ticks == UINT64_MAX && jelli_habits_valid(&huge));
    huge.lifetime_meals = UINT32_MAX;
    jelli_habits_record_meal(&huge, UINT64_MAX);
    CHECK(huge.lifetime_meals == UINT32_MAX && jelli_habits_valid(&huge));
}

static void thresholds_and_validation(void)
{
    JelliHabits h = {0};
    CHECK(jelli_habits_social_gain(&h, 250u) == 250u); /* Exactly 75%. */
    jelli_habits_advance(&h, 0u, JELLI_HABIT_DAY_TICKS, false, false);
    CHECK(jelli_habits_social_gain(&h, 250u) == 200u);
    jelli_habits_advance(&h, h.cursor_ticks, 2u * JELLI_HABIT_HOUR_TICKS, true, false);
    CHECK(jelli_habits_sleep_score(&h) == 250u);
    CHECK(jelli_habits_social_gain(&h, 250u) == 250u); /* Exactly 25%. */
    jelli_habits_advance(&h, h.cursor_ticks, 4u * JELLI_HABIT_HOUR_TICKS, true, false);
    CHECK(jelli_habits_sleep_score(&h) == 750u);
    CHECK(jelli_habits_social_gain(&h, 250u) == 250u);
    jelli_habits_advance(&h, h.cursor_ticks, 288u, true, false);
    CHECK(jelli_habits_sleep_score(&h) == 751u);
    CHECK(jelli_habits_social_gain(&h, 250u) == 300u);
    h.bins[h.head].sleep_ticks = 36000u;
    h.bins[h.head].play_ticks = 1u;
    CHECK(!jelli_habits_valid(&h));
    CHECK(!jelli_habits_valid(NULL));
}

int main(void)
{
    provisional_prior_and_targets();
    oldest_hour_fades();
    chunking_and_huge_elapsed();
    thresholds_and_validation();
    puts("PASS: bounded rolling habits, provisional scores, fading and saturation");
    return 0;
}

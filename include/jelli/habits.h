#ifndef JELLI_HABITS_H
#define JELLI_HABITS_H

#include <stdbool.h>
#include <stdint.h>

#define JELLI_HABIT_BIN_COUNT 25u
#define JELLI_HABIT_HOUR_TICKS UINT64_C(36000)
#define JELLI_HABIT_DAY_TICKS UINT64_C(864000)

typedef struct {
    uint16_t sleep_ticks, play_ticks, meals;
} JelliHabitBin;

typedef struct {
    JelliHabitBin bins[JELLI_HABIT_BIN_COUNT];
    uint64_t lifetime_sleep_ticks, lifetime_play_ticks, observed_ticks, cursor_ticks;
    uint32_t lifetime_meals;
    uint8_t head;
} JelliHabits;

typedef struct {
    uint64_t sleep_ticks, play_ticks, meals_q16, coverage_ticks;
} JelliHabitTotals;

/* Zero initialization records unknown history, never invented sleep or meals.
 * Times are injected pet simulation ticks, not wall timestamps. */
bool jelli_habits_valid(const JelliHabits *habits);
void jelli_habits_advance(JelliHabits *habits, uint64_t start_ticks, uint64_t elapsed_ticks,
                          bool asleep, bool playing);
void jelli_habits_record_meal(JelliHabits *habits, uint64_t current_ticks);
JelliHabitTotals jelli_habits_totals(const JelliHabits *habits);
/* Unknown coverage contributes a provisional 75% prior, fading over 24 hours.
 * Targets: eight sleeping hours, three meals, ten playing minutes per day. */
uint16_t jelli_habits_sleep_score(const JelliHabits *habits);
uint16_t jelli_habits_food_score(const JelliHabits *habits);
uint16_t jelli_habits_play_score(const JelliHabits *habits);
/* Applies the sleep factor once to a bounded positive social or fun gain. */
unsigned jelli_habits_social_gain(const JelliHabits *habits, unsigned amount);

#endif

#include "jelli/habits.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static uint64_t add_saturated(uint64_t value, uint64_t amount)
{
    return amount > UINT64_MAX - value ? UINT64_MAX : value + amount;
}

bool jelli_habits_valid(const JelliHabits *h)
{
    if (!h || h->head >= JELLI_HABIT_BIN_COUNT || h->observed_ticks > h->cursor_ticks ||
        h->lifetime_play_ticks > h->observed_ticks ||
        h->lifetime_sleep_ticks > h->observed_ticks - h->lifetime_play_ticks)
        return false;
    uint64_t sleep = 0u, play = 0u, meals = 0u;
    for (unsigned i = 0u; i < JELLI_HABIT_BIN_COUNT; ++i) {
        const JelliHabitBin *b = &h->bins[i];
        if ((unsigned)b->sleep_ticks + b->play_ticks > JELLI_HABIT_HOUR_TICKS)
            return false;
        sleep += b->sleep_ticks;
        play += b->play_ticks;
        meals += b->meals;
    }
    return sleep <= h->lifetime_sleep_ticks && play <= h->lifetime_play_ticks &&
           meals <= h->lifetime_meals;
}

static void move_cursor(JelliHabits *h, uint64_t ticks)
{
    uint64_t hours = ticks / JELLI_HABIT_HOUR_TICKS - h->cursor_ticks / JELLI_HABIT_HOUR_TICKS;
    if (hours >= JELLI_HABIT_BIN_COUNT) {
        memset(h->bins, 0, sizeof(h->bins));
        h->head = (uint8_t)(((uint64_t)h->head + hours) % JELLI_HABIT_BIN_COUNT);
    } else {
        for (uint64_t i = 0u; i < hours; ++i) {
            h->head = (uint8_t)(((unsigned)h->head + 1u) % JELLI_HABIT_BIN_COUNT);
            h->bins[h->head] = (JelliHabitBin){0};
        }
    }
    h->cursor_ticks = ticks;
}

static void record_duration(JelliHabits *h, uint64_t ticks, bool asleep, bool playing)
{
    uint64_t end = h->cursor_ticks + ticks;
    uint64_t capacity = JELLI_HABIT_HOUR_TICKS * JELLI_HABIT_BIN_COUNT;
    if (ticks > capacity)
        move_cursor(h, end - capacity);
    /* At most 26 portions, even when elapsed time spans years. */
    for (unsigned i = 0u; i <= JELLI_HABIT_BIN_COUNT && h->cursor_ticks < end; ++i) {
        uint64_t room = JELLI_HABIT_HOUR_TICKS - h->cursor_ticks % JELLI_HABIT_HOUR_TICKS;
        uint64_t part = end - h->cursor_ticks;
        if (part > room)
            part = room;
        JelliHabitBin *b = &h->bins[h->head];
        if (asleep)
            b->sleep_ticks = (uint16_t)(b->sleep_ticks + part);
        else if (playing)
            b->play_ticks = (uint16_t)(b->play_ticks + part);
        move_cursor(h, h->cursor_ticks + part);
    }
}

void jelli_habits_advance(JelliHabits *h, uint64_t start, uint64_t ticks, bool asleep, bool playing)
{
    if (!h || start < h->cursor_ticks)
        return;
    move_cursor(h, start);
    if (ticks > UINT64_MAX - start)
        ticks = UINT64_MAX - start;
    h->observed_ticks = add_saturated(h->observed_ticks, ticks);
    if (asleep)
        h->lifetime_sleep_ticks = add_saturated(h->lifetime_sleep_ticks, ticks);
    else if (playing)
        h->lifetime_play_ticks = add_saturated(h->lifetime_play_ticks, ticks);
    record_duration(h, ticks, asleep, playing);
}

void jelli_habits_record_meal(JelliHabits *h, uint64_t ticks)
{
    if (!h || ticks < h->cursor_ticks)
        return;
    move_cursor(h, ticks);
    if (h->bins[h->head].meals < UINT16_MAX)
        ++h->bins[h->head].meals;
    if (h->lifetime_meals < UINT32_MAX)
        ++h->lifetime_meals;
}

JelliHabitTotals jelli_habits_totals(const JelliHabits *h)
{
    JelliHabitTotals t = {0};
    if (!h)
        return t;
    t.coverage_ticks =
        h->observed_ticks > JELLI_HABIT_DAY_TICKS ? JELLI_HABIT_DAY_TICKS : h->observed_ticks;
    for (unsigned age = 0u; age < JELLI_HABIT_BIN_COUNT; ++age) {
        unsigned index = ((unsigned)h->head + JELLI_HABIT_BIN_COUNT - age) % JELLI_HABIT_BIN_COUNT;
        const JelliHabitBin *b = &h->bins[index];
        uint64_t weight = age == 24u
                              ? JELLI_HABIT_HOUR_TICKS - h->cursor_ticks % JELLI_HABIT_HOUR_TICKS
                              : JELLI_HABIT_HOUR_TICKS;
        t.sleep_ticks += (uint64_t)b->sleep_ticks * weight / JELLI_HABIT_HOUR_TICKS;
        t.play_ticks += (uint64_t)b->play_ticks * weight / JELLI_HABIT_HOUR_TICKS;
        t.meals_q16 += (uint64_t)b->meals * UINT64_C(65536) * weight / JELLI_HABIT_HOUR_TICKS;
    }
    return t;
}

static uint16_t score(uint64_t amount, uint64_t target, uint64_t coverage)
{
    uint64_t prior =
        target * 3u * (JELLI_HABIT_DAY_TICKS - coverage) / (4u * JELLI_HABIT_DAY_TICKS);
    uint64_t value = (amount + prior) * 1000u / target;
    return value > 1000u ? 1000u : (uint16_t)value;
}

uint16_t jelli_habits_sleep_score(const JelliHabits *h)
{
    JelliHabitTotals t = jelli_habits_totals(h);
    return score(t.sleep_ticks, 8u * JELLI_HABIT_HOUR_TICKS, t.coverage_ticks);
}

uint16_t jelli_habits_food_score(const JelliHabits *h)
{
    JelliHabitTotals t = jelli_habits_totals(h);
    return score(t.meals_q16, 3u * UINT64_C(65536), t.coverage_ticks);
}

uint16_t jelli_habits_play_score(const JelliHabits *h)
{
    JelliHabitTotals t = jelli_habits_totals(h);
    return score(t.play_ticks, UINT64_C(6000), t.coverage_ticks);
}

unsigned jelli_habits_social_gain(const JelliHabits *h, unsigned amount)
{
    uint16_t sleep = jelli_habits_sleep_score(h);
    unsigned factor = sleep > 750u ? 120u : sleep < 250u ? 80u : 100u;
    /* Callers grant needs bounded by 1000; saturation also protects this API. */
    if (amount > 1000u)
        amount = 1000u;
    return amount * factor / 100u;
}

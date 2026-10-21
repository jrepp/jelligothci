#include "jelli/activities.h"
#include "jelli/behavior.h"
#include "jelli/nutrition.h"
#include "game_internal.h"
#include <string.h>

#define USEFUL_MEAL 128u
#define PULSE_MASK 127u
#define MAX_PULSES 32u

static unsigned duration(const JelliPet *pet)
{
    if (pet->moment && pet->moment <= jelli_moment_count)
        return (unsigned)jelli_moments[pet->moment - 1u].duration_s * 10u;
    if (pet->activity == JELLI_EXERCISING)
        return jelli_exercise.duration_ticks;
    if (pet->activity == JELLI_PLAYING)
        return 80u;
    if (pet->activity == JELLI_CLEANING)
        return JELLI_CLEAN_TICKS;
    return 50u;
}

static unsigned pulse_count(const JelliPet *pet)
{
    unsigned count = duration(pet) / 15u + 1u;
    return count < 2u ? 2u : count > MAX_PULSES ? MAX_PULSES : count;
}

static uint32_t roll(const JelliPet *pet, unsigned pulse, unsigned salt)
{
    /* Intentional unsigned wrapping; independent of the gameplay RNG. */
    uint32_t seed = (uint32_t)pet->interaction_due ^ (uint32_t)(pet->interaction_due >> 32) ^
                    pet->id * UINT32_C(2654435761) ^ (pulse + salt) * UINT32_C(2246822519);
    seed ^= seed >> 16;
    seed *= UINT32_C(3266489917);
    return seed ^ (seed >> 13);
}

static unsigned pulse_time(const JelliPet *pet, unsigned pulse)
{
    unsigned span = duration(pet), intervals = pulse_count(pet) - 1u;
    if (!pulse || pulse == intervals)
        return pulse ? span : 0u;
    unsigned jitter = span / intervals / 5u;
    return pulse * span / intervals - jitter + roll(pet, pulse, 13u) % (2u * jitter + 1u);
}

static unsigned weight(const JelliPet *pet, unsigned pulse)
{
    return 80u + roll(pet, pulse, 47u) % 41u;
}

static void pay(JelliPet *pet, unsigned pulse)
{
    unsigned before = 0u, total = 0u, count = pulse_count(pet);
    for (unsigned i = 0u; i < count; ++i) {
        unsigned value = weight(pet, i);
        total += value;
        if (i < pulse)
            before += value;
    }
    if (!total)
        return; /* Keep the division safe even if a future schedule has no pulses. */
    unsigned after = before + weight(pet, pulse);
    for (unsigned i = 0u; i < JELLI_ACTIVITY_METERS; ++i) {
        unsigned reward = pet->moment_rewards[i];
        unsigned delta = reward * after / total - reward * before / total;
        uint16_t *meter = i < JELLI_NEED_COUNT            ? &pet->needs[i]
                          : i == JELLI_ACTIVITY_HYDRATION ? &pet->hydration
                                                          : &pet->bond;
        unsigned sum = *meter + delta;
        *meter = (uint16_t)(sum > 1000u ? 1000u : sum);
        if (sum >= 1000u && i < JELLI_NEED_COUNT)
            pet->need_remainders[i] = 0u;
        if (sum >= 1000u && i == JELLI_ACTIVITY_HYDRATION)
            pet->hydration_remainder = 0u;
    }
    pet->moment_pulse = (uint8_t)((pet->moment_pulse & USEFUL_MEAL) | (pulse + 1u));
}

static void legacy_totals(JelliGame *game, JelliPet *pet)
{
    switch ((JelliActivity)pet->activity) {
    case JELLI_EATING:
        --game->food;
        pet->moment_rewards[JELLI_SATIETY] = jelli_foods[pet->food_type].fullness;
        pet->moment_rewards[JELLI_ACTIVITY_HYDRATION] = jelli_foods[pet->food_type].hydration;
        break;
    case JELLI_PLAYING:
        pet->needs[JELLI_ENERGY] =
            pet->needs[JELLI_ENERGY] > 50u ? (uint16_t)(pet->needs[JELLI_ENERGY] - 50u) : 0u;
        pet->needs[JELLI_HYGIENE] =
            pet->needs[JELLI_HYGIENE] > 20u ? (uint16_t)(pet->needs[JELLI_HYGIENE] - 20u) : 0u;
        pet->moment_rewards[JELLI_AMUSEMENT] =
            (uint16_t)jelli_habits_social_gain(&pet->habits, 250u);
        break;
    case JELLI_EXERCISING:
        pet->moment_rewards[JELLI_AMUSEMENT] =
            (uint16_t)jelli_habits_social_gain(&pet->habits, jelli_exercise.amusement_gain);
        break;
    case JELLI_CLEANING:
        pet->moment_rewards[JELLI_HYGIENE] = 350u;
        break;
    case JELLI_GIVING:
        --game->gifts;
        pet->moment_rewards[JELLI_ACTIVITY_BOND] = 25u;
        break;
    case JELLI_IDLE:
    case JELLI_CARING:
        break;
    }
}

void jelli_activity_rewards_clear(JelliPet *pet)
{
    memset(pet->moment_rewards, 0, sizeof(pet->moment_rewards));
    pet->moment_pulse = 0u;
}

void jelli_activity_rewards_begin(JelliGame *game, JelliPet *pet)
{
    jelli_activity_rewards_clear(pet);
    if (pet->activity == JELLI_IDLE || pet->activity == JELLI_CARING ||
        (pet->moment && (pet->behavior_flags & JELLI_PET_FLAG_FREE_PLAY)))
        return;
    if (pet->activity == JELLI_EATING && pet->needs[JELLI_SATIETY] < 700u)
        pet->moment_pulse = USEFUL_MEAL;
    if (pet->moment) {
        unsigned id = pet->moment - 1u;
        unsigned factor =
            jelli_moment_bonus_percent(pet, id) * jelli_moment_jitter_percent(pet, id);
        for (unsigned i = 0u; i < JELLI_ACTIVITY_METERS; ++i) {
            unsigned gain = ((uint32_t)jelli_moments[id].gains[i] * factor + 5000u) / 10000u;
            if (i == JELLI_AMUSEMENT || i == JELLI_SOCIAL)
                gain = jelli_habits_social_gain(&pet->habits, gain);
            pet->moment_rewards[i] = (uint16_t)(gain + jelli_game_preference_gain(game, pet, i));
        }
    } else {
        legacy_totals(game, pet);
    }
    pay(pet, 0u);
}

void jelli_activity_rewards_tick(JelliGame *game, JelliPet *pet)
{
    unsigned pulse = pet->moment_pulse & PULSE_MASK;
    if (!pulse || pulse >= pulse_count(pet))
        return;
    uint64_t start = pet->interaction_due - duration(pet);
    if (pet->ticks < start || pet->ticks - start < pulse_time(pet, pulse))
        return;
    JelliEventSnapshot before = jelli_game_observe(game, pet);
    pay(pet, pulse);
    jelli_game_emit(game, JELLI_EVENT_EFFECT, pet->activity, JELLI_OK, pulse, pet, before);
}

bool jelli_activity_rewards_valid(const JelliPet *pet)
{
    unsigned pulse = pet->moment_pulse & PULSE_MASK;
    for (unsigned i = 0u; i < JELLI_ACTIVITY_METERS; ++i)
        if (pet->moment_rewards[i] > 10000u || (!pulse && pet->moment_rewards[i]))
            return false;
    if (!pulse)
        return pet->moment_pulse == 0u;
    return pet->activity != JELLI_IDLE && pet->activity != JELLI_CARING &&
           pulse <= pulse_count(pet) && pet->interaction_due >= duration(pet) &&
           (!(pet->moment_pulse & USEFUL_MEAL) || pet->activity == JELLI_EATING);
}

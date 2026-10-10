#include "jelli/activities.h"
#include "jelli/behavior.h"
#include "jelli/nutrition.h"
#include "jelli/potty.h"
#include "game_internal.h"

static uint16_t *meter(JelliPet *pet, unsigned index)
{
    if (index == JELLI_ACTIVITY_HYDRATION)
        return &pet->hydration;
    if (index == JELLI_ACTIVITY_BOND)
        return &pet->bond;
    return &pet->needs[index];
}

unsigned jelli_activity_cost(const JelliPet *pet, unsigned index, unsigned base)
{
    unsigned percent = 100u;
    for (unsigned i = 0u; i < jelli_activity_pet_cost_count; ++i) {
        const JelliActivityPetCosts *cost = &jelli_activity_pet_costs[i];
        if (cost->form != pet->form)
            continue;
        if (index == JELLI_ENERGY)
            percent = cost->energy_pct;
        if (index == JELLI_ACTIVITY_HYDRATION)
            percent = cost->hydration_pct;
        break;
    }
    return (base * percent + 99u) / 100u;
}

unsigned jelli_moment_cost(const JelliPet *pet, unsigned id, unsigned index)
{
    if (id >= jelli_moment_count || index >= JELLI_ACTIVITY_METERS)
        return 0u;
    return jelli_activity_cost(pet, index, jelli_moments[id].costs[index]);
}

unsigned jelli_moment_location(const JelliPet *pet, unsigned id)
{
    if (id >= jelli_moment_count)
        return pet->location;
    const JelliMoment *m = &jelli_moments[id];
    if (!m->randomize_location && (m->locations & (1u << pet->location)))
        return pet->location;
    if (m->locations == 3u && m->randomize_location) {
        uint64_t start = pet->interaction_due - (uint64_t)m->duration_s * 10u;
        /* Stable accepted start time; no menu/check RNG mutation. */
        uint32_t seed = (uint32_t)start ^ (uint32_t)(start >> 32) ^ pet->id ^ id;
        seed ^= seed >> 16;
        seed *= UINT32_C(3266489917);
        seed ^= seed >> 13;
        return seed & 1u;
    }
    return (m->locations & 1u) ? 0u : 1u;
}

const char *jelli_moment_cost_hint(const JelliPet *pet, unsigned id)
{
    static const char *const hints[JELLI_ACTIVITY_METERS] = {
        "EAT FIRST",     "REST FIRST",  "CLEAN FIRST",     "PLAY FIRST",
        "CONNECT FIRST", "DRINK FIRST", "BUILD BOND FIRST"};
    if (id >= jelli_moment_count)
        return "";
    for (unsigned i = 0u; i < JELLI_ACTIVITY_METERS; ++i) {
        unsigned value = i < JELLI_NEED_COUNT            ? pet->needs[i]
                         : i == JELLI_ACTIVITY_HYDRATION ? pet->hydration
                                                         : pet->bond;
        unsigned cost = jelli_moment_cost(pet, id, i);
        unsigned floor = i < JELLI_NEED_COUNT && pet->health == JELLI_RECOVERING ? 400u : 0u;
        if (cost && value < cost + floor)
            return hints[i];
    }
    return "";
}

void jelli_moment_charge(JelliGame *game, JelliPet *pet, unsigned id)
{
    const JelliMoment *m = &jelli_moments[id];
    /* The command validates every cost before mutating any meter or inventory. */
    for (unsigned i = 0u; i < JELLI_ACTIVITY_METERS; ++i) {
        uint16_t *value = meter(pet, i);
        *value = (uint16_t)(*value - jelli_moment_cost(pet, id, i));
    }
    if (m->kind == JELLI_MOMENT_FEED)
        --game->food;
}

unsigned jelli_moment_bonus_percent(const JelliPet *pet, unsigned id)
{
    if (id >= jelli_moment_count)
        return 100u;
    const JelliMoment *m = &jelli_moments[id];
    for (unsigned i = 0u; i < m->bonus_count; ++i)
        if (m->bonuses[i].form == pet->form)
            return 100u + m->bonuses[i].percent;
    /* Existing behavior affinities remain a fallback, never a second multiplier. */
    return jelli_behavior_moment_percent(pet, id);
}

unsigned jelli_moment_jitter_percent(const JelliPet *pet, unsigned id)
{
    if (id >= jelli_moment_count)
        return 100u;
    /* Saved deadline + identity fixes the roll for this accepted activity, including
     * reload/offline completion. Unsigned hash wrapping is intentional. */
    uint32_t seed = (uint32_t)pet->interaction_due ^ (uint32_t)(pet->interaction_due >> 32) ^
                    pet->id * UINT32_C(2654435761) ^ id * UINT32_C(2246822519);
    seed ^= seed >> 16;
    seed *= UINT32_C(3266489917);
    seed ^= seed >> 13;
    unsigned jitter = jelli_moments[id].jitter_pct;
    return 100u - jitter + seed % (2u * jitter + 1u);
}

static void reward_meter(JelliPet *pet, unsigned i, unsigned amount)
{
    if (i == JELLI_AMUSEMENT || i == JELLI_SOCIAL)
        amount = jelli_habits_social_gain(&pet->habits, amount);
    uint16_t *value = meter(pet, i);
    unsigned sum = *value + amount;
    *value = (uint16_t)(sum > 1000u ? 1000u : sum);
    if (sum >= 1000u && i < JELLI_NEED_COUNT)
        pet->need_remainders[i] = 0u;
    if (sum >= 1000u && i == JELLI_ACTIVITY_HYDRATION)
        pet->hydration_remainder = 0u;
}

void jelli_moment_reward(const JelliGame *game, JelliPet *pet)
{
    if (!pet->moment || pet->moment > jelli_moment_count)
        return;
    unsigned id = pet->moment - 1u;
    const JelliMoment *m = &jelli_moments[id];
    bool useful_meal = m->kind == JELLI_MOMENT_FEED && pet->needs[JELLI_SATIETY] < 700u;
    unsigned factor = jelli_moment_bonus_percent(pet, id) * jelli_moment_jitter_percent(pet, id);
    for (unsigned i = 0u; i < JELLI_ACTIVITY_METERS; ++i)
        if (m->gains[i])
            reward_meter(pet, i, (unsigned)(((uint32_t)m->gains[i] * factor + 5000u) / 10000u));
    jelli_game_preference(game, pet);
    if (m->kind == JELLI_MOMENT_FEED) {
        jelli_habits_record_meal(&pet->habits, pet->ticks);
        jelli_potty_eat(pet);
        if (useful_meal && !pet->reward_claimed) {
            pet->reward_pending = true;
            if (pet->feeds < UINT16_MAX)
                ++pet->feeds;
        }
    }
    if (m->gains[JELLI_ACTIVITY_HYDRATION])
        jelli_potty_drink(pet);
}

#include "game_internal.h"
#include "jelli/collection.h"
#include "jelli/nutrition.h"

#include <limits.h>
#include <stddef.h>

#define MINUTE_TICKS UINT64_C(600)

static uint64_t saturating_add(uint64_t left, uint64_t right)
{
    return (UINT64_MAX - left < right) ? UINT64_MAX : left + right;
}

static uint16_t apply_need_delta(uint16_t value, int64_t delta, uint16_t floor)
{
    int64_t result = (int64_t)value + delta;
    if (result < floor)
        return floor;
    if (result > 1000)
        return 1000u;
    return (uint16_t)result;
}

static void integrate_need(JelliPet *pet, JelliNeed need, uint64_t ticks, uint32_t rate,
                           bool increase)
{
    uint64_t minutes = ticks / MINUTE_TICKS;
    uint32_t tail = (uint32_t)(ticks % MINUTE_TICKS);
    int32_t signed_rate = increase ? (int32_t)rate : -(int32_t)rate;
    int64_t fractional = (int64_t)tail * signed_rate - pet->need_remainders[need];
    int64_t quotient = fractional / (int64_t)MINUTE_TICKS;
    int64_t remainder = fractional % (int64_t)MINUTE_TICKS;
    if (remainder < 0) {
        --quotient;
        remainder += (int64_t)MINUTE_TICKS;
    }
    int64_t whole_delta = (int64_t)minutes * signed_rate + quotient;
    if (remainder > 0) {
        ++whole_delta;
        pet->need_remainders[need] = (uint16_t)((int64_t)MINUTE_TICKS - remainder);
    } else {
        pet->need_remainders[need] = 0u;
    }
    uint16_t floor = (pet->health == JELLI_RECOVERING) ? 400u : 0u;
    int64_t candidate = (int64_t)pet->needs[need] + whole_delta;
    if (candidate < floor || (candidate == floor && pet->need_remainders[need] > 0u) ||
        candidate > 1000)
        pet->need_remainders[need] = 0u;
    pet->needs[need] = apply_need_delta(pet->needs[need], whole_delta, floor);
}

static uint64_t slower_ticks(const JelliPet *pet, uint64_t ticks, unsigned divisor)
{
    /* Absolute injected phase preserves fractional decay across host batches
     * and sleep transitions without changing the saved remainder units. */
    return pet->ticks / divisor - (pet->ticks - ticks) / divisor;
}

static void integrate_needs(JelliPet *pet, uint64_t ticks)
{
    if (pet->asleep) {
        integrate_need(pet, JELLI_ENERGY, ticks, 10u, true);
        /* Rest saves appetite/cleanliness and preserves fun and connection. */
        integrate_need(pet, JELLI_SATIETY, slower_ticks(pet, ticks, 4u), 1u, false);
        integrate_need(pet, JELLI_HYGIENE, slower_ticks(pet, ticks, 8u), 1u, false);
    } else {
        integrate_need(pet, JELLI_SATIETY, ticks, 1u, false);
        unsigned energy_rate = jelli_habits_sleep_score(&pet->habits) < 250u ? 5u : 4u;
        integrate_need(pet, JELLI_ENERGY, ticks, energy_rate, false);
        integrate_need(pet, JELLI_HYGIENE, slower_ticks(pet, ticks, 4u), 1u, false);
        integrate_need(pet, JELLI_AMUSEMENT, ticks, 2u, false);
        integrate_need(pet, JELLI_SOCIAL, ticks, 2u, false);
    }
}

static void adjust_need(JelliPet *pet, JelliNeed need, int32_t delta)
{
    if (delta > 0 && (need == JELLI_AMUSEMENT || need == JELLI_SOCIAL))
        delta = (int32_t)jelli_habits_social_gain(&pet->habits, (unsigned)delta);
    uint16_t floor = (pet->health == JELLI_RECOVERING) ? 400u : 0u;
    int64_t candidate = (int64_t)pet->needs[need] + delta;
    if (candidate < floor || (candidate == floor && pet->need_remainders[need] > 0u) ||
        candidate > 1000)
        pet->need_remainders[need] = 0u;
    pet->needs[need] = apply_need_delta(pet->needs[need], delta, floor);
}

void jelli_game_add_clock(JelliGame *game, JelliPet *pet, uint64_t ticks)
{
    (void)game;
    uint64_t old_ticks = pet->ticks;
    pet->ticks = saturating_add(pet->ticks, ticks);
    pet->stage_ticks = saturating_add(pet->stage_ticks, ticks);
    integrate_needs(pet, pet->ticks - old_ticks);
    jelli_hydration_advance(pet, pet->ticks - old_ticks);
    jelli_habits_advance(&pet->habits, old_ticks, pet->ticks - old_ticks, pet->asleep,
                         pet->activity == JELLI_PLAYING);
    jelli_pet_touch_decay(pet, pet->ticks - old_ticks);
}

bool jelli_game_window(const JelliPet *pet, uint64_t *remaining_ticks)
{
    uint64_t now = (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset) % JELLI_DAY_TICKS;
    uint64_t start = (uint64_t)pet->bedtime * UINT64_C(36000);
    uint64_t elapsed = (now + JELLI_DAY_TICKS - start) % JELLI_DAY_TICKS;
    if (elapsed < pet->sleep_duration) {
        if (remaining_ticks != NULL)
            *remaining_ticks = (uint64_t)pet->sleep_duration - elapsed;
        return true;
    }
    if (remaining_ticks != NULL)
        *remaining_ticks = (start + JELLI_DAY_TICKS - now) % JELLI_DAY_TICKS;
    return false;
}

static bool needs_safe_for_sleep_healing(const JelliPet *pet)
{
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (pet->needs[i] < 400u)
            return false;
    }
    return true;
}

static void update_health_and_hunger(JelliPet *pet)
{
    if (pet->asleep && pet->health == JELLI_UNWELL && needs_safe_for_sleep_healing(pet))
        pet->health = JELLI_WELL;
    if (pet->health != JELLI_RECOVERING &&
        (pet->needs[JELLI_SATIETY] <= 100u || pet->needs[JELLI_ENERGY] <= 100u ||
         pet->needs[JELLI_HYGIENE] <= 100u || pet->needs[JELLI_AMUSEMENT] <= 100u ||
         pet->needs[JELLI_SOCIAL] <= 100u))
        pet->health = JELLI_UNWELL;
    if (pet->needs[JELLI_SATIETY] > 350u) {
        pet->hunger_low = false;
        pet->hunger_counted = false;
        pet->hunger_due = 0u;
    } else if (pet->needs[JELLI_SATIETY] <= 200u && !pet->hunger_low && !pet->asleep) {
        pet->hunger_low = true;
        pet->hunger_counted = false;
        pet->hunger_due = saturating_add(pet->ticks, UINT64_C(6000));
    } else if (pet->hunger_low && !pet->hunger_counted && !pet->asleep &&
               pet->ticks >= pet->hunger_due) {
        pet->neglect = (pet->neglect == UINT16_MAX) ? UINT16_MAX : (uint16_t)(pet->neglect + 1u);
        pet->hunger_counted = true;
    }
}

static void reset_hunger_grace(JelliPet *pet)
{
    if (pet->needs[JELLI_SATIETY] <= 200u && !pet->hunger_low) {
        pet->hunger_low = true;
        pet->hunger_counted = false;
    }
    if (pet->hunger_low && !pet->hunger_counted)
        pet->hunger_due = saturating_add(pet->ticks, UINT64_C(6000));
}

void jelli_game_apply_effect(JelliGame *game, JelliPet *pet)
{
    JelliEventSnapshot before = jelli_game_observe(game, pet);
    unsigned activity = (unsigned)pet->activity;
    switch (pet->activity) {
    case JELLI_EATING:
        if (game->food > 0u) {
            bool useful = pet->needs[JELLI_SATIETY] < 700u;
            --game->food;
            jelli_habits_record_meal(&pet->habits, pet->ticks);
            const JelliFood *food = &jelli_foods[pet->food_type];
            adjust_need(pet, JELLI_SATIETY, food->fullness);
            if (food->hydration)
                jelli_hydration_add(pet, food->hydration);
            adjust_need(pet, JELLI_HYGIENE, -10);
            if (useful && !pet->reward_claimed) {
                pet->reward_pending = true;
                if (pet->feeds < UINT16_MAX)
                    ++pet->feeds;
            }
        }
        pet->activity = JELLI_IDLE;
        pet->interaction_due = 0u;
        break;
    case JELLI_PLAYING:
        adjust_need(pet, JELLI_AMUSEMENT, 250);
        adjust_need(pet, JELLI_ENERGY, -50);
        adjust_need(pet, JELLI_HYGIENE, -20);
        pet->activity = JELLI_IDLE;
        pet->interaction_due = 0u;
        break;
    case JELLI_EXERCISING:
        adjust_need(pet, JELLI_AMUSEMENT, jelli_exercise.amusement_gain);
        pet->activity = JELLI_IDLE;
        pet->interaction_due = 0u;
        break;
    case JELLI_CLEANING:
        adjust_need(pet, JELLI_HYGIENE, 350);
        pet->activity = JELLI_IDLE;
        pet->interaction_due = 0u;
        break;
    case JELLI_CARING:
        pet->health = JELLI_WELL;
        pet->activity = JELLI_IDLE;
        pet->interaction_due = 0u;
        break;
    case JELLI_GIVING:
        if (game->gifts > 0u) {
            --game->gifts;
            pet->bond = (pet->bond > 975u) ? 1000u : (uint16_t)(pet->bond + 25u);
        }
        pet->activity = JELLI_IDLE;
        pet->interaction_due = 0u;
        break;
    case JELLI_IDLE:
        break;
    }
    if (activity != JELLI_IDLE)
        jelli_game_emit(game, JELLI_EVENT_EFFECT, activity, JELLI_OK, 0u, pet, before);
}

static void evolve_if_due(JelliPet *pet)
{
    if (pet->form == 0u && !(pet->reached_forms & 2u) &&
        pet->stage_ticks >= jelli_collection_growth_ticks) {
        pet->stage_ticks -= jelli_collection_growth_ticks;
        pet->form = 1u;
        pet->reached_forms |= 3u;
        if (pet->health == JELLI_RECOVERING) {
            for (size_t i = 0u; i < JELLI_NEED_COUNT; ++i) {
                if (pet->needs[i] <= 400u) {
                    pet->needs[i] = 400u;
                    pet->need_remainders[i] = 0u;
                }
            }
        }
    }
}

static void resolve_sleep(const JelliGame *game, JelliPet *pet)
{
    if (game->sleep_log.active && game->sleep_log.pet_id == pet->id) {
        if (!pet->asleep && pet->activity == JELLI_IDLE) {
            pet->asleep = true;
            pet->scheduled_sleep = false;
            pet->nap_due = UINT64_MAX;
        }
        return;
    }
    uint64_t remaining = 0u;
    bool in_window = jelli_game_window(pet, &remaining);
    if (pet->asleep) {
        bool ended = pet->scheduled_sleep
                         ? !in_window
                         : (pet->ticks >= pet->nap_due || pet->needs[JELLI_ENERGY] >= 800u);
        if (ended) {
            pet->asleep = false;
            pet->scheduled_sleep = false;
            pet->nap_due = 0u;
            pet->awake_until = saturating_add(pet->ticks, MINUTE_TICKS * 60u);
            reset_hunger_grace(pet);
        }
    } else if (in_window && pet->ticks >= pet->wake_override_until && pet->activity == JELLI_IDLE) {
        pet->asleep = true;
        pet->scheduled_sleep = true;
        pet->nap_due = 0u;
    } else if (!in_window && pet->needs[JELLI_ENERGY] <= 150u && pet->ticks >= pet->awake_until &&
               pet->activity == JELLI_IDLE) {
        pet->asleep = true;
        pet->scheduled_sleep = false;
        pet->nap_due = saturating_add(pet->ticks, MINUTE_TICKS * 60u);
    }
}

void jelli_game_endpoint(JelliGame *game, JelliPet *pet, uint64_t ticks, bool offline)
{
    uint64_t previous_ticks = pet->ticks;
    jelli_game_add_clock(game, pet, ticks);
    if (pet->ticks == previous_ticks)
        return;
    if (pet->activity != JELLI_IDLE && pet->interaction_due <= pet->ticks)
        jelli_game_apply_effect(game, pet);
    JelliEventSnapshot before = jelli_game_observe(game, pet);
    uint8_t form = pet->form;
    update_health_and_hunger(pet);
    evolve_if_due(pet);
    resolve_sleep(game, pet);
    if (before.health != (uint8_t)pet->health || ((before.flags & 1u) != 0u) != pet->asleep ||
        form != pet->form)
        jelli_game_emit(game, JELLI_EVENT_STATUS, form != pet->form ? 1u : 0u, JELLI_OK, pet->form,
                        pet, before);
    (void)offline;
}

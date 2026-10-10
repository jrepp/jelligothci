#include "jelli/behavior.h"
#include "jelli/collection.h"
#include "jelli/potty.h"
#include "game_internal.h"

#include <stddef.h>

#define STIMULUS_QUEUE_CAPACITY                                                                    \
    (sizeof(((JelliGame *)0)->stimuli) / sizeof(((JelliGame *)0)->stimuli[0]))
#define NEED_BITS ((1u << JELLI_NEED_COUNT) - 1u)
#define NEED_MAX 1000

static const JelliRepertoire *repertoire_of(const JelliPet *pet)
{
    if (pet->form >= jelli_collection_form_count)
        return NULL;
    unsigned index = jelli_behavior_form_repertoire[pet->form];
    return index < jelli_behavior_repertoire_count ? &jelli_behavior_repertoires[index] : NULL;
}

const JelliTouchRules *jelli_behavior_touch_rules(const JelliPet *pet)
{
    const JelliRepertoire *rep = repertoire_of(pet);
    return &jelli_behavior_touch[rep ? (unsigned)(rep - jelli_behavior_repertoires) + 1u : 0u];
}

const JelliBehaviorState *jelli_behavior_current(const JelliPet *pet)
{
    return pet->behavior && pet->behavior <= jelli_behavior_state_count
               ? &jelli_behavior_states[pet->behavior - 1u]
               : NULL;
}

/* Stateless deterministic draw: same pet, tick and salt always give the same value, so
 * replay never depends on frame rate and existing PRNG streams are left untouched. */
static uint32_t roll(const JelliPet *pet, uint32_t salt)
{
    uint32_t x = pet->id * UINT32_C(0x9e3779b1) ^ (uint32_t)pet->ticks ^
                 (uint32_t)(pet->ticks >> 32) * UINT32_C(0x85ebca6b) ^ salt * UINT32_C(0xc2b2ae35);
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    return x ^ (x >> 16);
}

void jelli_behavior_stimulus(JelliGame *game, unsigned kind, unsigned value)
{
    if (kind >= JELLI_STIM_COUNT)
        return;
    if (game->stimulus_count >= STIMULUS_QUEUE_CAPACITY) {
        if (game->stimuli_dropped < UINT8_MAX)
            ++game->stimuli_dropped;
        return;
    }
    game->stimuli[game->stimulus_count][0] = (uint8_t)kind;
    game->stimuli[game->stimulus_count][1] = (uint8_t)(value > UINT8_MAX ? UINT8_MAX : value);
    ++game->stimulus_count;
}

static unsigned day_minute(const JelliPet *pet, uint64_t ticks)
{
    return (unsigned)((ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) %
                      JELLI_DAY_TICKS / 600u);
}

static bool is_night(unsigned minute)
{
    unsigned start = jelli_behavior_rules.night_start_minute;
    unsigned end = jelli_behavior_rules.night_end_minute;
    return start > end ? (minute >= start || minute < end) : (minute >= start && minute < end);
}

static uint16_t adjusted(uint16_t value, int delta)
{
    int result = (int)value + delta;
    return (uint16_t)(result < 0 ? 0 : result > NEED_MAX ? NEED_MAX : result);
}

static void end_state(JelliGame *game, JelliPet *pet, bool answered)
{
    const JelliBehaviorState *state = jelli_behavior_current(pet);
    if (!state)
        return;
    JelliEventSnapshot before = jelli_game_observe(game, pet);
    if (answered)
        pet->bond = adjusted(pet->bond, state->request_bond);
    pet->cooldown_state = pet->behavior;
    uint32_t cooldown = state->cooldown_ticks / JELLI_BEHAVIOR_TICKS;
    pet->cooldown_left = (uint16_t)(cooldown > UINT16_MAX ? UINT16_MAX : cooldown);
    unsigned ended = pet->behavior - 1u;
    pet->behavior = 0u;
    pet->behavior_left = 0u;
    jelli_game_emit(game, JELLI_EVENT_STATUS,
                    answered ? JELLI_STATUS_BEHAVIOR_ANSWERED : JELLI_STATUS_BEHAVIOR_ENDED,
                    JELLI_OK, ended, pet, before);
}

static void enter_state(JelliGame *game, JelliPet *pet, unsigned index, uint32_t draw)
{
    const JelliBehaviorState *state = &jelli_behavior_states[index];
    JelliEventSnapshot before = jelli_game_observe(game, pet);
    for (unsigned need = 0u; need < JELLI_NEED_COUNT; ++need)
        pet->needs[need] = adjusted(pet->needs[need], state->needs[need]);
    pet->bond = adjusted(pet->bond, state->bond);
    uint32_t span = state->max_ticks - state->min_ticks + 1u;
    uint32_t seconds =
        (state->min_ticks + draw % span + JELLI_BEHAVIOR_TICKS - 1u) / JELLI_BEHAVIOR_TICKS;
    pet->behavior = (uint8_t)(index + 1u);
    pet->behavior_left = (uint16_t)(seconds ? (seconds > UINT16_MAX ? UINT16_MAX : seconds) : 1u);
    jelli_game_emit(game, JELLI_EVENT_STATUS, JELLI_STATUS_BEHAVIOR_BEGAN, JELLI_OK, index, pet,
                    before);
}

static bool reaction_fits(const JelliBehaviorReaction *r, const JelliPet *pet, unsigned kind,
                          unsigned value)
{
    if (r->on != kind || (r->value != JELLI_BEHAVIOR_ANY && r->value != value) ||
        r->state >= jelli_behavior_state_count)
        return false;
    if (pet->cooldown_left && pet->cooldown_state == r->state + 1u)
        return false;
    if (r->location != JELLI_BEHAVIOR_ANY && r->location != pet->location)
        return false;
    bool night = is_night(day_minute(pet, pet->ticks));
    if ((r->night == JELLI_WHEN_YES && !night) || (r->night == JELLI_WHEN_NO && night))
        return false;
    unsigned mood = jelli_pet_mood(pet);
    return mood >= r->mood_min && mood <= r->mood_max && pet->bond >= r->bond_min;
}

/* Weighted pick among fitting reactions, then that reaction's chance roll. */
static void react(JelliGame *game, JelliPet *pet, unsigned kind, unsigned value)
{
    const JelliRepertoire *rep = repertoire_of(pet);
    if (!rep || pet->asleep || pet->activity != JELLI_IDLE)
        return;
    unsigned total = 0u;
    for (unsigned i = 0u; i < rep->reaction_count; ++i) {
        const JelliBehaviorReaction *r = &jelli_behavior_reactions[rep->first_reaction + i];
        total += reaction_fits(r, pet, kind, value) ? r->weight : 0u;
    }
    if (!total)
        return;
    uint32_t salt = (uint32_t)kind << 8 | value;
    unsigned pick = roll(pet, salt) % total;
    for (unsigned i = 0u; i < rep->reaction_count; ++i) {
        const JelliBehaviorReaction *r = &jelli_behavior_reactions[rep->first_reaction + i];
        if (!reaction_fits(r, pet, kind, value))
            continue;
        if (pick >= r->weight) {
            pick -= r->weight;
            continue;
        }
        if (roll(pet, salt ^ UINT32_C(0x5bd1e995)) % 100u < r->chance_pct)
            enter_state(game, pet, r->state, roll(pet, salt + 1u));
        return;
    }
}

static void deliver(JelliGame *game, JelliPet *pet, unsigned kind, unsigned value)
{
    const JelliBehaviorState *state = jelli_behavior_current(pet);
    if (state && (state->ends_on & (1u << kind)))
        end_state(game, pet, false);
    /* A running state is only ended by its ends_on stimuli or its timer, never replaced. */
    if (!pet->behavior)
        react(game, pet, kind, value);
}

static void count_down(JelliGame *game, JelliPet *pet, uint64_t seconds)
{
    uint16_t step = (uint16_t)(seconds > UINT16_MAX ? UINT16_MAX : seconds);
    pet->cooldown_left = pet->cooldown_left > step ? (uint16_t)(pet->cooldown_left - step) : 0u;
    if (!pet->cooldown_left)
        pet->cooldown_state = 0u;
    if (!pet->behavior)
        return;
    if (pet->behavior_left > step && !pet->asleep) {
        pet->behavior_left = (uint16_t)(pet->behavior_left - step);
        return;
    }
    const JelliBehaviorState *state = jelli_behavior_current(pet);
    bool accident = state && state->on_timeout == JELLI_TIMEOUT_ACCIDENT && !pet->asleep;
    end_state(game, pet, false);
    if (accident) {
        JelliEventSnapshot before = jelli_game_observe(game, pet);
        jelli_potty_accident(pet);
        jelli_game_emit(game, JELLI_EVENT_STATUS, JELLI_STATUS_ACCIDENT, JELLI_OK, 0u, pet, before);
    }
}

/* Edges observed from state: night/morning, newly low needs, and waking. */
static void observe_edges(JelliGame *game, JelliPet *pet, uint64_t old_ticks)
{
    bool was_night = is_night(day_minute(pet, old_ticks));
    bool night = is_night(day_minute(pet, pet->ticks));
    if (night != was_night)
        jelli_behavior_stimulus(game, night ? JELLI_STIM_NIGHT : JELLI_STIM_MORNING, 0u);
    unsigned low = 0u;
    for (unsigned need = 0u; need < JELLI_NEED_COUNT; ++need)
        low |= pet->needs[need] < jelli_behavior_rules.need_low ? 1u << need : 0u;
    for (unsigned need = 0u; need < JELLI_NEED_COUNT; ++need)
        if ((low & ~(unsigned)pet->behavior_flags) & (1u << need))
            jelli_behavior_stimulus(game, JELLI_STIM_NEED_LOW, need);
    if ((pet->behavior_flags & JELLI_PET_FLAG_WAS_ASLEEP) && !pet->asleep)
        jelli_behavior_stimulus(game, JELLI_STIM_WOKE, pet->wake_mood);
    pet->behavior_flags = (uint8_t)(low | (pet->behavior_flags & JELLI_PET_FLAG_MESS) |
                                    (pet->asleep ? JELLI_PET_FLAG_WAS_ASLEEP : 0u));
}

void jelli_behavior_step(JelliGame *game, JelliPet *pet, uint64_t old_ticks, bool offline)
{
    uint64_t seconds = pet->ticks / JELLI_BEHAVIOR_TICKS - old_ticks / JELLI_BEHAVIOR_TICKS;
    if (!seconds)
        return;
    count_down(game, pet, seconds);
    if (offline) {
        /* Catch-up only expires states; it never starts new ones (RFC-005). */
        game->stimulus_count = 0u;
        pet->behavior_flags = (uint8_t)((pet->behavior_flags & (NEED_BITS | JELLI_PET_FLAG_MESS)) |
                                        (pet->asleep ? JELLI_PET_FLAG_WAS_ASLEEP : 0u));
        return;
    }
    observe_edges(game, pet, old_ticks);
    if (!pet->behavior && !pet->asleep && pet->activity == JELLI_IDLE)
        jelli_behavior_stimulus(game, JELLI_STIM_IDLE, 0u);
    unsigned count = game->stimulus_count;
    game->stimulus_count = 0u;
    for (unsigned i = 0u; i < count && i < STIMULUS_QUEUE_CAPACITY; ++i)
        deliver(game, pet, game->stimuli[i][0], game->stimuli[i][1]);
}

static unsigned activity_code(const JelliPet *pet)
{
    return pet->moment ? JELLI_ACTIVITY_CODE_MOMENT + pet->moment - 1u
                       : JELLI_ACTIVITY_CODE_CARE + (unsigned)pet->activity;
}

void jelli_behavior_command(JelliGame *game, JelliCommand command, unsigned before_location,
                            unsigned before_activity)
{
    JelliPet *pet = &game->pets[game->active];
    if (command.kind == JELLI_CMD_ACTIVATE) {
        game->stimulus_count = 0u; /* Queued stimuli belonged to the previous pet. */
        return;
    }
    const JelliBehaviorState *state = jelli_behavior_current(pet);
    if (state && state->request_command == (uint8_t)command.kind &&
        (state->request_value == JELLI_BEHAVIOR_ANY || state->request_value == command.value))
        end_state(game, pet, true);
    if (before_location != pet->location)
        jelli_behavior_stimulus(game, JELLI_STIM_LOCATION, pet->location);
    if (before_activity == JELLI_IDLE && pet->activity != JELLI_IDLE)
        jelli_behavior_stimulus(game, JELLI_STIM_ACTIVITY_STARTED, activity_code(pet));
    if (command.kind == JELLI_CMD_HEALTH)
        jelli_behavior_stimulus(game, JELLI_STIM_ACTIVITY_FINISHED,
                                JELLI_ACTIVITY_CODE_HEALTH + command.value);
    else if (command.kind == JELLI_CMD_GIFT)
        jelli_behavior_stimulus(game, JELLI_STIM_PRESENT_GIVEN, JELLI_BEHAVIOR_ANY);
    else if (command.kind == JELLI_CMD_TOUCH)
        jelli_behavior_stimulus(game, JELLI_STIM_TOUCHED, pet->reaction);
}

unsigned jelli_behavior_moment_percent(const JelliPet *pet, unsigned moment)
{
    const JelliRepertoire *rep = repertoire_of(pet);
    for (unsigned i = 0u; rep && i < rep->affinity_count; ++i) {
        const JelliAffinity *a = &jelli_behavior_affinities[rep->first_affinity + i];
        if (a->moment == moment)
            return 100u + a->bonus_pct;
    }
    return 100u;
}

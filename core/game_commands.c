#include "jelli/potty.h"
#include "jelli/wake.h"
#include "jelli/collection.h"
#include "game_internal.h"
#include "jelli/activities.h"
#include "jelli/behavior.h"
#include "jelli/nutrition.h"

#include <limits.h>
#include <stddef.h>

static uint8_t find_pet(const JelliGame *game, uint32_t id)
{
    for (uint8_t i = 0u; i < game->count; ++i) {
        if (game->pets[i].id == id)
            return i;
    }
    return JELLI_PET_CAPACITY;
}

static bool begin_activity(JelliPet *pet, JelliActivity activity, uint64_t duration)
{
    if (UINT64_MAX - pet->ticks < duration)
        return false;
    pet->activity = activity;
    pet->moment = 0u; /* A replacement activity must not retain a cancelled moment. */
    pet->interaction_due = pet->ticks + duration;
    return true;
}

static uint64_t deadline_after(const JelliPet *pet, uint64_t duration)
{
    return UINT64_MAX - pet->ticks < duration ? UINT64_MAX : pet->ticks + duration;
}

static JelliResult claim_reward(JelliGame *game, JelliPet *pet)
{
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (!pet->reward_pending || pet->reward_claimed)
        return JELLI_NOT_READY;
    if ((uint32_t)game->food + 3u > JELLI_STACK_LIMIT)
        return JELLI_FULL;
    game->food = (uint16_t)(game->food + 3u);
    pet->reward_pending = false;
    pet->reward_claimed = true;
    return JELLI_OK;
}

static JelliResult activate_pet(JelliGame *game, JelliCommand command, const JelliPet *active)
{
    uint8_t target_index = find_pet(game, command.value);
    if (target_index >= game->count)
        return JELLI_INVALID_TARGET;
    if (game->resuming || game->sleep_log.active || active->activity != JELLI_IDLE)
        return JELLI_NOT_READY;
    if (target_index == game->active)
        return JELLI_NOT_READY;
    game->active = target_index;
    return JELLI_OK;
}

static JelliResult start_care(JelliPet *pet)
{
    if (pet->activity == JELLI_CARING)
        return JELLI_BUSY;
    if (UINT64_MAX - pet->ticks < 300u)
        return JELLI_NOT_READY;
    pet->activity = JELLI_IDLE;
    pet->interaction_due = 0u;
    pet->asleep = false;
    pet->scheduled_sleep = false;
    pet->nap_due = 0u;
    for (size_t i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (pet->needs[i] <= 400u) {
            pet->needs[i] = 400u;
            pet->need_remainders[i] = 0u;
        }
    }
    pet->health = JELLI_RECOVERING;
    (void)begin_activity(pet, JELLI_CARING, 300u);
    return JELLI_OK;
}

static bool bedtime_pending(const JelliPet *pet)
{
    return jelli_game_window(pet, NULL) && pet->ticks >= pet->wake_override_until;
}

static JelliResult start_feed(const JelliGame *game, JelliPet *pet, uint32_t food)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (bedtime_pending(pet))
        return JELLI_BUSY;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (food >= jelli_food_count)
        return JELLI_INVALID_TARGET;
    if (game->food == 0u)
        return JELLI_NO_ITEM;
    if (!begin_activity(pet, JELLI_EATING, 50u))
        return JELLI_NOT_READY;
    pet->food_type = (uint8_t)food;
    return JELLI_OK;
}

static JelliResult start_play(JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (bedtime_pending(pet))
        return JELLI_BUSY;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    return begin_activity(pet, JELLI_PLAYING, 80u) ? JELLI_OK : JELLI_NOT_READY;
}

static JelliResult start_clean(JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    return begin_activity(pet, JELLI_CLEANING, JELLI_CLEAN_TICKS) ? JELLI_OK : JELLI_NOT_READY;
}

static JelliResult start_gift(const JelliGame *game, JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (bedtime_pending(pet))
        return JELLI_BUSY;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (game->gifts == 0u)
        return JELLI_NO_ITEM;
    return begin_activity(pet, JELLI_GIVING, 50u) ? JELLI_OK : JELLI_NOT_READY;
}

static JelliResult rest(JelliGame *game, JelliPet *pet)
{
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (UINT64_MAX - pet->ticks < UINT64_C(36000))
        return JELLI_NOT_READY;
    if (!jelli_sleep_log_begin(&game->sleep_log, pet->id, pet->ticks, game->wall_known,
                               game->wall_seconds))
        return JELLI_NOT_READY;
    unsigned last = ((unsigned)game->sleep_log.head + JELLI_SLEEP_SESSION_CAPACITY - 1u) %
                    JELLI_SLEEP_SESSION_CAPACITY;
    JelliSleepSession *session = &game->sleep_log.sessions[last];
    session->bed_energy = pet->needs[JELLI_ENERGY];
    session->bed_sleep_score = jelli_habits_sleep_score(&pet->habits);
    session->flags |= JELLI_SLEEP_STATS_KNOWN;
    pet->rest_ticks = 0u;
    pet->asleep = true;
    pet->scheduled_sleep = false;
    pet->nap_due = UINT64_MAX;
    return JELLI_OK;
}

static JelliResult wake(JelliGame *game, JelliPet *pet)
{
    bool linked = game->sleep_log.active && game->sleep_log.pet_id == pet->id;
    if (!pet->asleep && !linked)
        return JELLI_NOT_READY;
    if (linked)
        (void)jelli_sleep_log_finish(&game->sleep_log, pet->ticks, game->wall_known,
                                     game->wall_seconds);
    uint64_t remaining = 0u;
    if (jelli_game_window(pet, &remaining))
        pet->wake_override_until = deadline_after(pet, remaining);
    jelli_wake_react(pet);
    pet->asleep = false;
    pet->scheduled_sleep = false;
    pet->nap_due = 0u;
    pet->awake_until = deadline_after(pet, UINT64_C(36000));
    if (pet->needs[JELLI_SATIETY] <= 200u && !pet->hunger_low) {
        pet->hunger_low = true;
        pet->hunger_counted = false;
    }
    if (pet->hunger_low && !pet->hunger_counted)
        pet->hunger_due = deadline_after(pet, UINT64_C(6000));
    return JELLI_OK;
}

static JelliResult travel(JelliPet *pet, uint32_t location)
{
    if (pet->activity != JELLI_IDLE)
        return JELLI_BUSY;
    if (location > 1u)
        return JELLI_INVALID_TARGET;
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (pet->location == location)
        return JELLI_NOT_READY;
    pet->location = (uint8_t)location;
    return JELLI_OK;
}

static JelliResult set_bedtime(JelliPet *pet, uint32_t hour)
{
    if (hour > 23u)
        return JELLI_INVALID_TARGET;
    if (pet->bedtime == hour)
        return JELLI_NOT_READY;
    pet->bedtime = hour;
    return JELLI_OK;
}

static bool boost_need(JelliPet *pet, JelliNeed need, unsigned amount)
{
    if (pet->needs[need] >= 1000u)
        return false;
    if (need == JELLI_AMUSEMENT || need == JELLI_SOCIAL)
        amount = jelli_habits_social_gain(&pet->habits, amount);
    unsigned value = pet->needs[need] + amount;
    pet->needs[need] = (uint16_t)(value > 1000u ? 1000u : value);
    pet->need_remainders[need] = 0u;
    return true;
}

unsigned jelli_pet_shot_goal(const JelliPet *pet)
{
    if (pet->shot_until > pet->ticks && pet->shot_goal)
        return pet->shot_goal;
    uint32_t value = pet->random_state ^ pet->id;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return 1u + value % 3u;
}

bool jelli_pet_health_ready(const JelliPet *pet, unsigned activity)
{
    if (activity == JELLI_HEALTH_MEDICINE)
        return pet->ticks >= pet->medicine_until;
    if (activity == JELLI_HEALTH_SHOT)
        return pet->ticks >= pet->shot_until || pet->shot_hits < pet->shot_goal;
    return activity < JELLI_HEALTH_COUNT;
}

static void remember_health(JelliPet *pet, unsigned activity)
{
    uint64_t due = UINT64_MAX - pet->ticks < 36000u ? UINT64_MAX : pet->ticks + 36000u;
    if (activity == JELLI_HEALTH_MEDICINE)
        pet->medicine_until = due;
    if (activity != JELLI_HEALTH_SHOT)
        return;
    if (pet->ticks >= pet->shot_until) {
        pet->shot_goal = (uint8_t)jelli_pet_shot_goal(pet);
        pet->shot_hits = 0u;
        pet->shot_until = due;
        pet->random_state = pet->random_state * UINT32_C(1664525) + UINT32_C(1013904223);
    }
    ++pet->shot_hits;
}

static unsigned health_gain(uint32_t activity)
{
    if (activity == JELLI_HEALTH_MEDICINE)
        return 10u;
    if (activity == JELLI_HEALTH_WASH)
        return 70u;
    return activity >= JELLI_HEALTH_FLOSS ? 20u : 40u; /* Dental steps are small. */
}

static JelliResult healthy_click(JelliPet *pet, uint32_t activity)
{
    if (activity >= JELLI_HEALTH_COUNT)
        return JELLI_INVALID_TARGET;
    if (pet->asleep)
        return JELLI_ASLEEP;
    if (activity == JELLI_HEALTH_POTTY)
        return pet->activity == JELLI_IDLE ? jelli_potty_break(pet) : JELLI_BUSY;
    if (!jelli_pet_health_ready(pet, activity))
        return JELLI_NOT_READY;
    bool recovery = activity == JELLI_HEALTH_MEDICINE || activity == JELLI_HEALTH_SHOT;
    if (pet->activity != JELLI_IDLE && !(recovery && pet->activity == JELLI_CARING))
        return JELLI_BUSY;
    JelliNeed need = activity == JELLI_HEALTH_STRETCH ? JELLI_ENERGY
                     : recovery                       ? JELLI_SOCIAL
                                                      : JELLI_HYGIENE;
    bool healing = recovery && pet->health == JELLI_UNWELL;
    if (!healing && pet->needs[need] == 1000u && pet->bond == 1000u)
        return JELLI_FULL;
    if (healing) {
        JelliResult result = start_care(pet);
        if (result != JELLI_OK)
            return result;
    }
    remember_health(pet, activity);
    (void)boost_need(pet, need, health_gain(activity));
    pet->bond = pet->bond > 995u ? 1000u : (uint16_t)(pet->bond + 5u);
    return JELLI_OK;
}

static JelliResult moment(const JelliGame *game, JelliPet *pet, uint32_t choice)
{
    if (choice >= jelli_moment_count || choice >= JELLI_MOMENT_CAPACITY)
        return JELLI_INVALID_TARGET;
    const JelliMoment *m = &jelli_moments[choice];
    if (m->kind == JELLI_MOMENT_FEED)
        return start_feed(game, pet, 0u);
    JelliResult result = start_play(pet);
    if (result != JELLI_OK)
        return result;
    unsigned percent = jelli_behavior_moment_percent(pet, choice); /* Species affinity. */
    for (unsigned need = 0u; need < JELLI_NEED_COUNT; ++need)
        if (m->gains[need])
            (void)boost_need(pet, (JelliNeed)need, m->gains[need] * percent / 100u);
    if (m->location != JELLI_MOMENT_STAY)
        pet->location = m->location == JELLI_MOMENT_GARDEN ? 1u : 0u;
    pet->moment = (uint8_t)(choice + 1u);
    return JELLI_OK;
}

static JelliResult set_volume(JelliGame *game, uint32_t value)
{
    if (value > JELLI_VOLUME_MAX)
        return JELLI_INVALID_TARGET;
    if (value == game->volume)
        return JELLI_FULL;
    game->volume = (uint8_t)value;
    ++game->revision;
    return JELLI_OK;
}

static JelliResult start_exercise(JelliPet *pet)
{
    return bedtime_pending(pet) ? JELLI_BUSY : jelli_start_exercise(pet);
}

static JelliResult dispatch_action(JelliGame *game, JelliCommand command, JelliPet *pet)
{
    switch (command.kind) {
    case JELLI_CMD_FORM:
        return jelli_collection_set_form(game, command.actor_id, command.value);
    case JELLI_CMD_EXERCISE:
        return start_exercise(pet);
    case JELLI_CMD_WATER:
        return jelli_drink_water(pet);
    case JELLI_CMD_FEED:
        return start_feed(game, pet, command.value);
    case JELLI_CMD_PLAY:
        return start_play(pet);
    case JELLI_CMD_CLEAN:
        return start_clean(pet);
    case JELLI_CMD_CARE:
        return start_care(pet);
    case JELLI_CMD_GIFT:
        return start_gift(game, pet);
    case JELLI_CMD_CLAIM:
        return claim_reward(game, pet);
    case JELLI_CMD_REST:
        return rest(game, pet);
    case JELLI_CMD_WAKE:
        return wake(game, pet);
    case JELLI_CMD_TRAVEL:
        return travel(pet, command.value);
    case JELLI_CMD_BEDTIME:
        return set_bedtime(pet, command.value);
    case JELLI_CMD_MOMENT:
        return moment(game, pet, command.value);
    case JELLI_CMD_TOUCH:
        return jelli_game_touch(pet);
    case JELLI_CMD_HEALTH:
        return healthy_click(pet, command.value);
    case JELLI_CMD_VOLUME:
        return set_volume(game, command.value);
    case JELLI_CMD_REFILL_FOOD:
        return jelli_refill_food(game);
    case JELLI_CMD_ACTIVATE:
        return activate_pet(game, command, pet);
    }
    return JELLI_INVALID_TARGET;
}

JelliResult jelli_game_command_impl(JelliGame *game, JelliCommand command)
{
    if (!jelli_game_valid(game))
        return JELLI_INVALID_TARGET;
    if (game->resuming)
        return JELLI_BUSY;
    JelliPet *pet = &game->pets[game->active];
    if (pet->id != command.actor_id && command.kind != JELLI_CMD_FORM)
        return JELLI_INVALID_TARGET;
    return dispatch_action(game, command, pet);
}

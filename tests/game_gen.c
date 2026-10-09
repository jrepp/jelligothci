/* Seeded generator of valid, edge-biased games for property tests (see game_gen.h). */
#include "game_gen.h"

#include <limits.h>

uint32_t game_gen_next(GameGen *g)
{
    uint32_t x = g->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->state = x;
    return x;
}

static uint32_t below(GameGen *g, uint32_t bound) { return game_gen_next(g) % bound; }

uint32_t game_gen_below(GameGen *g, uint32_t bound) { return below(g, bound); }

static uint64_t saturating_add(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

/* A deadline before, at, or after `ticks`. */
static uint64_t near(GameGen *g, uint64_t ticks)
{
    uint64_t offset = below(g, 4u) ? below(g, 80000u) : 0u;
    if (below(g, 2u))
        return ticks >= offset ? ticks - offset : 0u;
    return saturating_add(ticks, offset);
}

static uint64_t random_ticks(GameGen *g)
{
    switch (below(g, 5u)) {
    case 0:
        return below(g, 2000u);
    case 1:
        return (uint64_t)below(g, 30u) * JELLI_DAY_TICKS + below(g, JELLI_DAY_TICKS);
    case 2:
        return (uint64_t)game_gen_next(g) << 24 | below(g, 1u << 24);
    case 3: /* Last minute before and at saturation. */
        return UINT64_MAX - below(g, 700u);
    default:
        return (uint64_t)below(g, 4u) * JELLI_DAY_TICKS + UINT64_C(324000) * below(g, 3u) +
               below(g, 3u);
    }
}

/* Values on and beside every threshold the rules use. */
static uint16_t random_need(GameGen *g)
{
    static const uint16_t edges[] = {0u,   1u,   100u, 101u, 150u, 151u, 200u, 201u, 350u,
                                     351u, 399u, 400u, 401u, 405u, 419u, 420u, 999u, 1000u};
    return below(g, 2u) ? edges[below(g, (uint32_t)(sizeof(edges) / sizeof(edges[0])))]
                        : (uint16_t)below(g, 1001u);
}

static void random_profile(GameGen *g, JelliPet *pet)
{
    pet->ticks = random_ticks(g);
    pet->form = (uint8_t)below(g, JELLI_FORM_COUNT);
    pet->stage_ticks = pet->form == JELLI_FORM_MINT ? below(g, 600u) : game_gen_next(g);
    pet->phase_offset = below(g, JELLI_DAY_TICKS);
    pet->bedtime = below(g, 24u);
    pet->sleep_duration = 600u + below(g, JELLI_DAY_TICKS - 1199u);
    pet->random_state = game_gen_next(g) | 1u;
    pet->bond = (uint16_t)(below(g, 2u) ? below(g, 1001u) : 1000u);
    pet->touch_load = (uint16_t)(below(g, 2u) ? below(g, 1001u) : 380u + below(g, 300u));
    pet->reaction = (uint8_t)below(g, 4u);
    pet->reaction_ticks = (uint8_t)(pet->reaction ? 1u + below(g, 30u) : 0u);
    pet->location = (uint8_t)below(g, 2u);
    pet->feeds = (uint16_t)below(g, 3u);
    pet->neglect = (uint16_t)(below(g, 8u) ? below(g, 5u) : UINT16_MAX);
    pet->wake_override_until = near(g, pet->ticks);
    pet->awake_until = near(g, pet->ticks);
    pet->medicine_until = near(g, pet->ticks);
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        pet->needs[i] = random_need(g);
        pet->need_remainders[i] = (uint16_t)(below(g, 3u) ? below(g, 600u) : 0u);
    }
}

static void random_lifecycle(GameGen *g, JelliPet *pet)
{
    pet->hunger_low = below(g, 3u) == 0u;
    pet->hunger_counted = pet->hunger_low && below(g, 2u);
    pet->hunger_due = pet->hunger_low ? near(g, pet->ticks) : 0u;
    unsigned reward = below(g, 3u);
    pet->reward_pending = reward == 1u;
    pet->reward_claimed = reward == 2u;
    pet->shot_goal = (uint8_t)below(g, 4u);
    pet->shot_hits = (uint8_t)(pet->shot_goal ? below(g, pet->shot_goal + 1u) : 0u);
    pet->shot_until = pet->shot_goal ? near(g, pet->ticks) | 1u : 0u;
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
        pet->prize_progress.counts[i] = (uint16_t)below(g, i == 7u ? 2u : 4u);
    pet->prize_progress.breakfast_day_known = below(g, 2u);
    pet->prize_progress.last_breakfast_day =
        pet->prize_progress.breakfast_day_known ? below(g, 1000u) : 0u;
    pet->health = below(g, 3u) ? JELLI_WELL : JELLI_UNWELL;
    unsigned sleep = below(g, 3u);
    pet->asleep = sleep != 0u;
    pet->scheduled_sleep = sleep == 1u;
    pet->nap_due = sleep == 2u ? (below(g, 4u) ? saturating_add(pet->ticks, 1u + below(g, 36000u))
                                               : UINT64_MAX)
                               : 0u;
    if (pet->asleep && !pet->scheduled_sleep && pet->ticks == UINT64_MAX)
        pet->nap_due = UINT64_MAX;
}

/* Recovering pets are caring, awake, and rest at or above the floor; often right on it. */
static void make_recovering(GameGen *g, JelliPet *pet)
{
    pet->health = JELLI_RECOVERING;
    pet->asleep = pet->scheduled_sleep = false;
    pet->nap_due = 0u;
    pet->activity = JELLI_CARING;
    pet->interaction_due = saturating_add(pet->ticks, below(g, 301u));
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (pet->needs[i] < 400u || below(g, 3u) == 0u)
            pet->needs[i] = (uint16_t)(400u + below(g, below(g, 2u) ? 25u : 601u));
        if (pet->needs[i] == 400u)
            pet->need_remainders[i] = 0u;
    }
}

static void random_activity(GameGen *g, JelliGame *game, JelliPet *pet)
{
    unsigned pick = below(g, 6u);
    if (pick == 0u)
        make_recovering(g, pet);
    if (pick < 2u || pet->asleep)
        return;
    static const JelliActivity busy[] = {JELLI_EATING, JELLI_PLAYING, JELLI_CLEANING, JELLI_GIVING};
    pet->activity = busy[below(g, 4u)];
    pet->interaction_due = saturating_add(pet->ticks, below(g, 81u));
    if (pet->activity == JELLI_EATING && game->food == 0u)
        game->food = 1u;
    if (pet->activity == JELLI_GIVING && game->gifts == 0u)
        game->gifts = 1u;
}

static void random_prizes(GameGen *g, JelliGame *game)
{
    JelliPrizes *prizes = &game->prizes;
    prizes->discovered = (uint16_t)below(g, JELLI_PRIZE_MASK + 1u);
    prizes->owned = (uint16_t)(prizes->discovered & below(g, JELLI_PRIZE_MASK + 1u));
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
        prizes->origin_pet[i] =
            (prizes->discovered >> i & 1u) ? game->pets[below(g, game->count)].id : 0u;
    unsigned offered = below(g, JELLI_PRIZE_COUNT + 1u);
    if (offered && !(prizes->owned >> (offered - 1u) & 1u)) {
        prizes->offered = (uint8_t)offered;
        prizes->offered_pet = game->pets[below(g, game->count)].id;
    }
}

/* Finished sessions for any pet, and sometimes a linked manual sleep for the active one,
 * in a shape play reaches: resting since REST (no nap deadline) or woken by care. */
static bool random_sleep_log(GameGen *g, JelliGame *game)
{
    JelliPet *pet = &game->pets[game->active];
    bool ok = true;
    for (unsigned i = below(g, 4u); i > 0u; --i) {
        uint64_t start = near(g, pet->ticks) / 2u;
        uint64_t wall = below(g, 2u) ? game_gen_next(g) : 0u;
        ok = ok &&
             jelli_sleep_log_begin(&game->sleep_log, game->pets[below(g, game->count)].id, start,
                                   wall != 0u, wall) &&
             jelli_sleep_log_finish(&game->sleep_log, start + below(g, 400000u), wall != 0u,
                                    wall + below(g, 50000u));
    }
    if (pet->asleep && below(g, 2u)) {
        pet->scheduled_sleep = false;
        pet->nap_due = UINT64_MAX;
    }
    bool resting = pet->asleep && !pet->scheduled_sleep && pet->nap_due == UINT64_MAX;
    if ((resting || pet->activity == JELLI_CARING) && below(g, 3u) == 0u)
        ok = ok && jelli_sleep_log_begin(&game->sleep_log, pet->id,
                                         pet->ticks - below(g, pet->ticks < 1000u ? 1u : 1000u),
                                         game->wall_known, game->wall_seconds);
    return ok;
}

bool game_gen_build(GameGen *g, JelliGame *game)
{
    jelli_game_init(game);
    game->count =
        (uint8_t)(JELLI_ROSTER_MIN + below(g, JELLI_PET_CAPACITY - JELLI_ROSTER_MIN + 1u));
    for (unsigned i = 0u; i < game->count; ++i) {
        JelliPet *pet = &game->pets[i];
        *pet = (JelliPet){.id = i + 1u, .health = JELLI_WELL, .activity = JELLI_IDLE};
        random_profile(g, pet);
        random_lifecycle(g, pet);
    }
    game->active = (uint8_t)below(g, game->count);
    game->food = (uint16_t)(below(g, 3u) ? below(g, JELLI_STACK_LIMIT + 1u) : 0u);
    game->gifts = (uint16_t)(below(g, 3u) ? below(g, JELLI_STACK_LIMIT + 1u) : JELLI_STACK_LIMIT);
    game->clock_known = below(g, 2u);
    game->clock_minute = (uint16_t)below(g, 1440u);
    game->wall_known = below(g, 2u);
    game->wall_seconds = game->wall_known ? game_gen_next(g) : 0u;
    game->timezone_minutes = (int16_t)((int)below(g, 1561u) - 720);
    game->ticks = random_ticks(g);
    random_activity(g, game, &game->pets[game->active]);
    random_prizes(g, game);
    bool logged = random_sleep_log(g, game);
    return logged && jelli_game_valid(game);
}

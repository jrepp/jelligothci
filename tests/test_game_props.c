/* Seeded property and metamorphic tests for the whole game core, not one command.
 * Each seed builds a valid game with edge-biased state (healthy, unwell, recovering at its
 * 400 floor, scheduled sleep, nap, mid-activity, full or empty stacks, offered prizes,
 * saturated clocks, mid-resume), then runs random commands of every kind with in- and
 * out-of-range values, touch bursts, prize catches/gifts/completions, host clock changes,
 * live frames, and offline catch-up of up to a day. After every step the game is valid
 * and survives a save round trip; a failed command, catch, or gift leaves it unchanged;
 * jelli_game_check predicts every command result and (outside preference moments) its
 * exact effect; time never touches an inactive pet. Failures print the seed and step; an
 * argument adds seeds. */
#include "game_gen.h"
#include "jelli/save.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_STEP(expr)                                                                           \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x step %u)\n", __FILE__, __LINE__, #expr,        \
                    (unsigned)w.seed, w.step);                                                     \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STEPS = 160, SEEDS = 48 };
enum { ACCEPTED, REFUSED, TOUCHES, FLOORED, CAUGHT, FRAMES, RESUMES, RESUMED, TALLIES };

static struct {
    uint32_t seed;
    GameGen gen;
    unsigned step;
    uint64_t n[TALLIES];
} w;
static JelliGame before, scratch, expected;
static JelliEventLog events;
static JelliSave saves[2];
static uint8_t encoded[2][JELLI_SAVE_CAPACITY];

static uint32_t next_random(void) { return game_gen_next(&w.gen); }

static uint32_t below(uint32_t bound) { return game_gen_below(&w.gen, bound); }

/* A generated game wired to an event log, sometimes left mid catch-up. */
static void generate(JelliGame *game)
{
    CHECK_STEP(game_gen_build(&w.gen, game));
    game->events = &events; /* Commands also record into a borrowed log. */
    if (below(8u) == 0u)
        jelli_game_resume_begin(game, below(300000u));
}

/* Saves hold checkpoints only: live backlog and catch-up are compared outside the codec. */
static void checkpoint(JelliSave *save, const JelliGame *game)
{
    *save = (JelliSave){.game = *game, .sequence = w.step};
    save->game.backlog_ms = 0u;
    save->game.resuming = false;
    save->game.resume_remaining_ms = 0u;
}

/* Field-for-field equality that never compares padding: saved state through the codec,
 * then the live clock, touch memory, host observations, and event sink directly. */
static bool same_game(const JelliGame *a, const JelliGame *b)
{
    if (a->backlog_ms != b->backlog_ms || a->resuming != b->resuming ||
        a->resume_remaining_ms != b->resume_remaining_ms || a->events != b->events)
        return false;
    checkpoint(&saves[0], a);
    checkpoint(&saves[1], b);
    size_t size = jelli_save_encode(&saves[0], encoded[0], sizeof(encoded[0]));
    if (size == 0u || jelli_save_encode(&saves[1], encoded[1], sizeof(encoded[1])) != size ||
        memcmp(encoded[0], encoded[1], size) != 0)
        return false;
    for (unsigned i = 0u; i < JELLI_PET_CAPACITY; ++i) {
        if (a->pets[i].touch_load != b->pets[i].touch_load ||
            a->pets[i].reaction != b->pets[i].reaction ||
            a->pets[i].reaction_ticks != b->pets[i].reaction_ticks)
            return false;
    }
    return a->clock_known == b->clock_known && a->clock_minute == b->clock_minute &&
           a->wall_known == b->wall_known && a->wall_seconds == b->wall_seconds;
}

/* The saved game decodes to a valid game that re-encodes to the same bytes. */
static void check_round_trip(const JelliGame *game)
{
    checkpoint(&saves[0], game);
    size_t size = jelli_save_encode(&saves[0], encoded[0], sizeof(encoded[0]));
    CHECK_STEP(size > 0u && jelli_save_decode(&saves[1], encoded[0], size));
    CHECK_STEP(jelli_game_valid(&saves[1].game));
    CHECK_STEP(jelli_save_encode(&saves[1], encoded[1], sizeof(encoded[1])) == size);
    CHECK_STEP(memcmp(encoded[0], encoded[1], size) == 0);
}

static uint32_t random_value(const JelliGame *game, unsigned kind)
{
    /* One past each kind's valid range, so out-of-range values are tried too. */
    static const uint32_t bounds[JELLI_CMD_TOUCH + 2u] = {[JELLI_CMD_TRAVEL] = 3u,
                                                          [JELLI_CMD_BEDTIME] = 25u,
                                                          [JELLI_CMD_MOMENT] = 5u,
                                                          [JELLI_CMD_HEALTH] = 10u};
    if (kind == JELLI_CMD_ACTIVATE)
        return below(game->count + 2u);
    if (bounds[kind])
        return below(bounds[kind]);
    return below(4u) ? 0u : next_random();
}

static JelliCommand random_command(const JelliGame *game)
{
    /* One past the last kind exercises the unknown-command path. */
    unsigned kind = below(3u) ? below(JELLI_CMD_TOUCH + 2u) : JELLI_CMD_TOUCH;
    uint32_t actor = game->pets[game->active].id;
    if (below(24u) == 0u)
        actor = below(JELLI_PET_CAPACITY + 2u);
    return (JelliCommand){(JelliCommandKind)kind, actor, random_value(game, kind)};
}

static void command_step(JelliGame *game, JelliCommand command)
{
    memcpy(&before, game, sizeof(before));
    JelliResult predicted = jelli_game_check(game, command, &scratch);
    JelliResult result = jelli_game_command(game, command);
    CHECK_STEP(predicted == result);
    if (result != JELLI_OK) {
        CHECK_STEP(same_game(&before, game));
        ++w.n[REFUSED];
        return;
    }
    ++w.n[ACCEPTED];
    CHECK_STEP(jelli_game_valid(game));
    /* Only a favourite moment adds a preference bonus beyond the checked dispatch. */
    scratch.events = game->events;
    CHECK_STEP(command.kind == JELLI_CMD_MOMENT || same_game(&scratch, game));
    if (command.kind != JELLI_CMD_TOUCH)
        return;
    const JelliPet *pet = &game->pets[game->active];
    ++w.n[TOUCHES];
    CHECK_STEP(pet->reaction >= 1u && pet->reaction_ticks == 30u);
    if (pet->health == JELLI_RECOVERING &&
        (pet->needs[JELLI_SOCIAL] == 400u || pet->needs[JELLI_AMUSEMENT] == 400u))
        ++w.n[FLOORED];
}

static void prize_step(JelliGame *game)
{
    memcpy(&before, game, sizeof(before));
    unsigned pick = below(3u);
    if (pick == 2u) {
        jelli_prize_complete(game, below(JELLI_PRIZE_GIFT + 2u));
        CHECK_STEP(jelli_game_valid(game));
        return;
    }
    unsigned index = below(JELLI_PRIZE_COUNT + 1u);
    JelliResult result = pick ? jelli_prize_gift(game, index) : jelli_prize_catch(game);
    if (result != JELLI_OK) {
        CHECK_STEP(same_game(&before, game));
        return;
    }
    ++w.n[CAUGHT];
    CHECK_STEP(jelli_game_valid(game));
    if (pick == 0u)
        CHECK_STEP(game->prizes.owned >> (before.prizes.offered - 1u) & 1u);
    else
        CHECK_STEP(!(game->prizes.owned >> index & 1u));
}

/* Time keeps the game valid, never runs a pet's clock backwards, and changes only the
 * active pet, the stacks its effects spend, and the clocks. */
static void check_time_step(const JelliGame *game)
{
    CHECK_STEP(jelli_game_valid(game));
    CHECK_STEP(game->pets[before.active].ticks >= before.pets[before.active].ticks);
    memcpy(&expected, &before, sizeof(expected));
    expected.pets[before.active] = game->pets[before.active];
    expected.food = game->food;
    expected.gifts = game->gifts;
    expected.ticks = game->ticks;
    expected.backlog_ms = game->backlog_ms;
    expected.discarded_ms = game->discarded_ms;
    expected.resuming = game->resuming;
    expected.resume_remaining_ms = game->resume_remaining_ms;
    CHECK_STEP(same_game(&expected, game));
}

static void live_step(JelliGame *game)
{
    static const uint32_t gaps[] = {0u, 1u, 16u, 99u, 100u, 800u, 2000u, 3600000u};
    memcpy(&before, game, sizeof(before));
    uint32_t elapsed = below(2u) ? gaps[below(8u)] : below(1200u);
    jelli_game_advance(game, elapsed);
    ++w.n[FRAMES];
    if (before.resuming) /* Live time waits for the catch-up. */
        CHECK_STEP(same_game(&before, game));
    check_time_step(game);
}

static void resume_step(JelliGame *game)
{
    static const uint32_t spans[] = {1u, 99u, 60000u, 60001u, 3600000u, 7200000u};
    uint64_t elapsed = below(2u) ? spans[below(6u)] : below(7200000u);
    if (below(24u) == 0u) /* Past the one-day cap. */
        elapsed = JELLI_OFFLINE_CAP_MS + below(7200000u);
    uint64_t start = game->pets[game->active].ticks;
    memcpy(&before, game, sizeof(before));
    jelli_game_resume_begin(game, elapsed);
    ++w.n[RESUMES];
    for (unsigned calls = 0u; !jelli_game_resume_step(game); ++calls) {
        CHECK_STEP(calls < 1440u && jelli_game_valid(game));
        /* The full comparison holds across any number of minutes; sample it. */
        if (calls % 16u == 15u) {
            check_time_step(game);
            memcpy(&before, game, sizeof(before));
        }
        /* Commands and live frames wait for the catch-up and change nothing. */
        if (below(32u) == 0u)
            command_step(game, random_command(game));
        else if (below(32u) == 0u)
            live_step(game);
    }
    w.n[RESUMED] += game->pets[game->active].ticks - start;
    check_time_step(game);
}

static void host_step(JelliGame *game)
{
    game->clock_known = below(2u);
    game->clock_minute = (uint16_t)below(1440u);
    game->wall_known = below(2u);
    game->wall_seconds = game->wall_known ? game->wall_seconds + below(100000u) : 0u;
    CHECK_STEP(jelli_game_valid(game));
}

static void step(JelliGame *game)
{
    unsigned pick = below(64u);
    if (pick < 30u) {
        command_step(game, random_command(game));
    } else if (pick < 34u) { /* Rapid touches overstimulate before the load decays. */
        JelliCommand touch = {JELLI_CMD_TOUCH, game->pets[game->active].id, 0u};
        for (unsigned i = 1u + below(5u); i > 0u; --i)
            command_step(game, touch);
    } else if (pick < 40u) {
        prize_step(game);
    } else if (pick < 60u) {
        live_step(game);
    } else if (pick < 62u) {
        resume_step(game);
    } else {
        host_step(game);
    }
    if (w.step % 8u == 0u)
        check_round_trip(game);
}

static void run_seed(uint32_t seed)
{
    w.seed = w.gen.state = seed;
    w.step = 0u;
    JelliGame game;
    generate(&game);
    if (game.resuming) {
        memcpy(&before, &game, sizeof(before));
        while (!jelli_game_resume_step(&game))
            check_time_step(&game);
    }
    for (w.step = 1u; w.step <= STEPS; ++w.step)
        step(&game);
}

/* Pinned from seed 0x019140a6: a Mint whose clock saturates kept counting stage time
 * without evolving (endpoints stop once the clock stops), so stage_ticks passed 600 on
 * a Mint and the game turned invalid. Stage time now follows the clock it was given. */
static void saturated_mint_stays_valid(void)
{
    w.seed = 0x019140a6u;
    JelliGame game;
    jelli_game_init(&game);
    JelliPet *pet = &game.pets[0];
    pet->ticks = UINT64_MAX - 5u;
    pet->stage_ticks = 100u;
    pet->phase_offset = 0u; /* Awake: the bedtime window is far away. */
    pet->awake_until = UINT64_MAX;
    CHECK_STEP(jelli_game_valid(&game));
    jelli_game_resume_begin(&game, 120000u);
    while (!jelli_game_resume_step(&game))
        CHECK_STEP(jelli_game_valid(&game));
    CHECK_STEP(jelli_game_valid(&game));
    CHECK_STEP(pet->ticks == UINT64_MAX && pet->form == JELLI_FORM_MINT);
    CHECK_STEP(pet->stage_ticks == 105u);
}

/* Reference: stage time counts only ticks the clock really moved; a mint evolves, keeping the
 * overflow, the moment that real time reaches one stage. Returns the pet's expected form. */
static uint8_t reference_stage(uint8_t form, uint64_t stage, uint64_t real, uint64_t *out)
{
    uint64_t total = UINT64_MAX - stage < real ? UINT64_MAX : stage + real;
    bool evolves = form == JELLI_FORM_MINT && total >= 600u;
    *out = evolves ? total - 600u : total;
    return evolves ? JELLI_FORM_LILAC : form;
}

static void staged_game(JelliGame *game, uint64_t origin, uint8_t form, uint64_t stage)
{
    jelli_game_init(game);
    game->pets[0].ticks = origin;
    game->pets[0].form = form;
    game->pets[0].stage_ticks = stage;
    game->pets[0].awake_until = UINT64_MAX;
    CHECK_STEP(jelli_game_valid(game));
}

/* Live ticks one at a time, then the batched offline path, both against the reference. */
static void check_stage_ticks(uint64_t origin, uint8_t form, uint64_t stage, uint64_t ticks)
{
    JelliGame game;
    uint64_t want;
    staged_game(&game, origin, form, stage);
    for (w.step = 1u; w.step <= ticks; ++w.step) {
        jelli_game_advance(&game, 100u);
        uint64_t real = UINT64_MAX - origin < w.step ? UINT64_MAX - origin : w.step;
        CHECK_STEP(jelli_game_valid(&game) && game.pets[0].ticks == origin + real);
        CHECK_STEP(game.pets[0].form == reference_stage(form, stage, real, &want));
        CHECK_STEP(game.pets[0].stage_ticks == want);
    }
    staged_game(&game, origin, form, stage);
    jelli_game_resume_begin(&game, ticks * 100u);
    while (!jelli_game_resume_step(&game))
        CHECK_STEP(jelli_game_valid(&game));
    uint64_t real = UINT64_MAX - origin < ticks ? UINT64_MAX - origin : ticks;
    CHECK_STEP(jelli_game_valid(&game) && game.pets[0].ticks == origin + real);
    CHECK_STEP(game.pets[0].form == reference_stage(form, stage, real, &want));
    CHECK_STEP(game.pets[0].stage_ticks == want);
}

/* Clocks pinned 0, 1, 5, or 600 ticks before saturation evolve exactly when real time reaches a
 * stage; far from saturation every requested tick is real, as before the fix. */
static void saturated_stage_counts_real_time(void)
{
    static const uint64_t gaps[] = {0u, 1u, 5u, 600u};
    static const uint64_t far[] = {0u, UINT64_C(1) << 40, UINT64_MAX - 5000u};
    for (size_t g = 0u; g < sizeof(gaps) / sizeof(gaps[0]); ++g) {
        const uint64_t stages[] = {0u, 1u, 594u, 595u, 599u, 599u - gaps[g], 600u - gaps[g]};
        for (size_t s = 0u; s < sizeof(stages) / sizeof(stages[0]); ++s) {
            for (uint8_t form = JELLI_FORM_MINT; form <= JELLI_FORM_LILAC && stages[s] < 600u;
                 ++form) {
                w.seed = (uint32_t)(g << 16 | s << 8 | form); /* Identifies the case. */
                check_stage_ticks(UINT64_MAX - gaps[g], form, stages[s], 610u);
                for (size_t o = 0u; o < sizeof(far) / sizeof(far[0]); ++o)
                    check_stage_ticks(far[o], form, stages[s], 610u);
            }
        }
    }
    check_stage_ticks(UINT64_C(1) << 40, JELLI_FORM_LILAC, UINT64_MAX - 2u, 5u); /* Saturates. */
}

int main(int argc, char **argv)
{
    unsigned extra = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0u;
    saturated_mint_stays_valid();
    saturated_stage_counts_real_time();
    for (unsigned i = 0u; i < SEEDS + extra; ++i)
        run_seed(0x9e3779b9u * (i + 1u) ^ 0x5bd1e995u);
    printf("game props: %u seeds, %" PRIu64 " accepted and %" PRIu64 " refused commands, %" PRIu64
           " touches (%" PRIu64 " recovering at the floor), %" PRIu64 " prizes, %" PRIu64
           " frames, %" PRIu64 " resumes over %.1f hours\n",
           SEEDS + extra, w.n[ACCEPTED], w.n[REFUSED], w.n[TOUCHES], w.n[FLOORED], w.n[CAUGHT],
           w.n[FRAMES], w.n[RESUMES], (double)w.n[RESUMED] / 36000.0);
    return 0;
}

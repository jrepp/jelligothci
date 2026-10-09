/* Seeded model tests for the screen sleep policy. Random sequences of time steps (zero,
 * backward, boundary, huge, and pinned near UINT64_MAX) and calls (activity, idle,
 * remaining, wake taps at clustered and extreme positions, cancel, guard, forced set)
 * run against an independent reference model written from the header contract and
 * memo-023. Every return value, the asleep flag, and the remaining time must agree after
 * every step. Metamorphic relations then rerun each sequence shifted in time, translated
 * and mirrored in space, and checks the same trace. A failure prints seed and step. */
#include "jelli/pet_screen.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK_STEP(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x step %u op %u)\n", __FILE__, __LINE__,         \
                    #expression, (unsigned)run.seed, run.step, run.op);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STEPS = 3000, OPS = 7 };
enum { OP_ACTIVITY, OP_IDLE, OP_TAP, OP_CANCEL, OP_GUARDED, OP_SET, OP_REMAINING };

/* Reference: the documented policy, kept deliberately naive (floating-point distance,
 * explicit comparisons) so it shares no arithmetic with core/pet_screen.c. */
typedef struct {
    uint64_t anchor, tap_ms, wake_ms;
    int tap_x, tap_y;
    bool dark, pending, guard;
} Model;

typedef struct {
    uint8_t op;
    bool flag;
    int x, y;
    uint64_t t;
} Step;

static struct {
    uint32_t seed, state;
    unsigned step, op;
    uint64_t now;
    Step steps[STEPS];
    uint64_t trace[STEPS]; /* Return value, asleep flag, and remaining, per step. */
    unsigned wakes, sleeps, guarded, backward;
} run;

static uint32_t next_random(void)
{
    uint32_t x = run.state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    run.state = x;
    return x;
}

static bool model_near(int ax, int ay, int bx, int by)
{
    double dx = (double)ax - (double)bx, dy = (double)ay - (double)by;
    return dx * dx + dy * dy <= (double)JELLI_SCREEN_DOUBLE_TAP_PX * JELLI_SCREEN_DOUBLE_TAP_PX;
}

static void model_set(Model *m, bool dark, uint64_t t)
{
    m->dark = dark;
    m->pending = false;
    m->guard = false;
    if (!dark)
        m->anchor = t;
}

static uint64_t model_remaining(const Model *m, uint64_t t)
{
    if (m->dark)
        return 0u;
    if (t < m->anchor)
        return JELLI_SCREEN_SLEEP_MS;
    uint64_t idle = t - m->anchor;
    return idle >= JELLI_SCREEN_SLEEP_MS ? 0u : JELLI_SCREEN_SLEEP_MS - idle;
}

/* Applies one step to the model; returns the value the API must return. */
static uint64_t model_apply(Model *m, const Step *s)
{
    switch (s->op) {
    case OP_ACTIVITY:
        m->anchor = s->t;
        return 0u;
    case OP_IDLE:
        if (m->dark)
            return 0u;
        if (s->t < m->anchor) {
            m->anchor = s->t;
            return 0u;
        }
        if (s->t - m->anchor < JELLI_SCREEN_SLEEP_MS)
            return 0u;
        model_set(m, true, s->t);
        return 1u;
    case OP_TAP: {
        if (!m->dark)
            return 0u;
        bool in_time =
            m->pending && s->t >= m->tap_ms && s->t - m->tap_ms <= JELLI_SCREEN_DOUBLE_TAP_MS;
        if (in_time && model_near(m->tap_x, m->tap_y, s->x, s->y)) {
            model_set(m, false, s->t);
            m->guard = true;
            m->wake_ms = s->t;
            return 1u;
        }
        m->pending = true; /* The window restarts from the latest tap. */
        m->tap_ms = s->t;
        m->tap_x = s->x;
        m->tap_y = s->y;
        return 0u;
    }
    case OP_CANCEL:
        m->pending = false;
        return 0u;
    case OP_GUARDED:
        if (m->guard && s->t >= m->wake_ms && s->t - m->wake_ms <= JELLI_SCREEN_DOUBLE_TAP_MS)
            return 1u;
        m->guard = false;
        return 0u;
    case OP_SET:
        model_set(m, s->flag, s->t);
        return 0u;
    default:
        return model_remaining(m, s->t);
    }
}

static uint64_t api_apply(JelliPetScreen *screen, const Step *s)
{
    switch (s->op) {
    case OP_ACTIVITY:
        jelli_pet_screen_activity(screen, s->t);
        return 0u;
    case OP_IDLE:
        return jelli_pet_screen_idle(screen, s->t) ? 1u : 0u;
    case OP_TAP:
        return jelli_pet_screen_wake_tap(screen, s->x, s->y, s->t) ? 1u : 0u;
    case OP_CANCEL:
        jelli_pet_screen_cancel_tap(screen);
        return 0u;
    case OP_GUARDED:
        return jelli_pet_screen_guarded(screen, s->t) ? 1u : 0u;
    case OP_SET:
        jelli_pet_screen_set(screen, s->flag, s->t);
        return 0u;
    default:
        return jelli_pet_screen_remaining(screen, s->t);
    }
}

/* Saturating step of the generator clock: mostly short gaps, with the boundaries of
 * both windows, zero, backward, and huge jumps mixed in. */
static uint64_t next_time(void)
{
    static const uint64_t edges[] = {499u, 500u, 501u, 179999u, 180000u, 180001u};
    uint32_t pick = next_random() % 32u;
    uint64_t step = 0u;
    if (pick < 6u)
        step = 0u;
    else if (pick < 18u)
        step = 1u + next_random() % 600u;
    else if (pick < 24u)
        step = edges[next_random() % 6u];
    else if (pick < 27u)
        step = (uint64_t)next_random() * (1u + next_random() % 4096u);
    else if (pick < 31u) {
        uint64_t back = 1u + next_random() % 200000u;
        ++run.backward;
        return run.now = run.now > back ? run.now - back : 0u;
    } else {
        step = 1u + next_random() % 60u;
    }
    return run.now = UINT64_MAX - run.now < step ? UINT64_MAX : run.now + step;
}

static int next_coordinate(int previous)
{
    static const int extremes[] = {INT_MIN, INT_MIN + 1, -1, 0, INT_MAX - 1, INT_MAX};
    uint32_t pick = next_random() % 16u;
    if (pick < 11u) /* Clustered around the last tap, straddling the 80 px radius. */
        return previous + (int)(next_random() % 241u) - 120;
    if (pick < 14u)
        return (int)(next_random() % 466u);
    return extremes[next_random() % 6u];
}

static void generate(uint32_t seed)
{
    run.seed = run.state = seed ? seed : 1u;
    uint32_t start = next_random() % 4u;
    run.now = start == 0u ? 0u : start == 1u ? UINT64_MAX - 3600000u : next_random();
    int x = 233, y = 233;
    for (unsigned i = 0u; i < STEPS; ++i) {
        Step *s = &run.steps[i];
        uint32_t pick = next_random() % 40u;
        /* Taps dominate so double taps and their guards occur often. */
        s->op = pick < 16u   ? OP_TAP
                : pick < 22u ? OP_IDLE
                : pick < 26u ? OP_ACTIVITY
                : pick < 29u ? OP_CANCEL
                : pick < 34u ? OP_GUARDED
                : pick < 37u ? OP_SET
                             : OP_REMAINING;
        s->flag = next_random() % 3u != 0u; /* Forced sleep more often than forced wake. */
        if (s->op == OP_TAP) {
            x = next_coordinate(x < -1000000 || x > 1000000 ? 233 : x);
            y = next_coordinate(y < -1000000 || y > 1000000 ? 233 : y);
        }
        s->x = x;
        s->y = y;
        s->t = next_time();
    }
}

/* Runs the generated steps with every time shifted by `shift` and every tap mapped by
 * (sx, sy, swap) plus (dx, dy); records or compares the trace against the model. */
static void replay(uint64_t shift, int sx, int sy, bool swap, int dx, int dy, bool record)
{
    JelliPetScreen screen;
    Model model = {.anchor = run.steps[0].t + shift};
    jelli_pet_screen_init(&screen, run.steps[0].t + shift);
    for (run.step = 0u; run.step < STEPS; ++run.step) {
        Step s = run.steps[run.step];
        run.op = s.op;
        int x = sx * (swap ? s.y : s.x) + dx, y = sy * (swap ? s.x : s.y) + dy;
        s.x = x;
        s.y = y;
        s.t += shift;
        uint64_t expected = model_apply(&model, &s), actual = api_apply(&screen, &s);
        CHECK_STEP(expected == actual);
        CHECK_STEP(screen.asleep == model.dark);
        uint64_t remaining = jelli_pet_screen_remaining(&screen, s.t);
        CHECK_STEP(remaining == model_remaining(&model, s.t));
        CHECK_STEP(remaining <= JELLI_SCREEN_SLEEP_MS);
        CHECK_STEP(!screen.asleep || remaining == 0u);
        uint64_t trace = (actual << 20) ^ ((uint64_t)screen.asleep << 19) ^ remaining;
        if (record) {
            run.trace[run.step] = trace;
            run.wakes += s.op == OP_TAP && actual;
            run.sleeps += s.op == OP_IDLE && actual;
            run.guarded += s.op == OP_GUARDED && actual;
        } else {
            CHECK_STEP(run.trace[run.step] == trace);
        }
    }
}

/* Taps are small numbers in these runs, so mirrored and translated copies cannot overflow. */
static bool small_taps(void)
{
    for (unsigned i = 0u; i < STEPS; ++i)
        if (run.steps[i].x < -1000000 || run.steps[i].x > 1000000 || run.steps[i].y < -1000000 ||
            run.steps[i].y > 1000000)
            return false;
    return true;
}

static uint64_t max_time(void)
{
    uint64_t top = 0u;
    for (unsigned i = 0u; i < STEPS; ++i)
        top = run.steps[i].t > top ? run.steps[i].t : top;
    return top;
}

static void run_seed(uint32_t seed, unsigned *metamorphic)
{
    generate(seed);
    replay(0u, 1, 1, false, 0, 0, true);
    replay(0u, 1, 1, false, 0, 0, false); /* Deterministic: same input, same trace. */
    uint64_t top = max_time();
    uint64_t shift = top < UINT64_MAX - 12345678u ? 12345678u : 0u;
    if (shift) { /* Only differences of time matter. */
        replay(shift, 1, 1, false, 0, 0, false);
        ++*metamorphic;
    }
    if (small_taps()) { /* Distance is invariant under translation, mirroring, and swap. */
        replay(0u, 1, 1, false, 4321, -777, false);
        replay(0u, -1, 1, false, 0, 0, false);
        replay(0u, 1, -1, true, 466, 0, false);
        ++*metamorphic;
    }
}

int main(int argc, char **argv)
{
    static const uint32_t pinned[] = {0x9e3779b9u, 0x243f6a88u, 0xdeadbeefu, 0x0badf00du};
    unsigned extra = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0u, metamorphic = 0u;
    unsigned seeds = 600u + extra, wakes = 0u, sleeps = 0u, guarded = 0u, backward = 0u;
    for (unsigned i = 0u; i < seeds; ++i) {
        run.wakes = run.sleeps = run.guarded = run.backward = 0u;
        run_seed(i < 4u ? pinned[i] : 0x85ebca6bu ^ (i * 0x9e3779b9u), &metamorphic);
        wakes += run.wakes;
        sleeps += run.sleeps;
        guarded += run.guarded;
        backward += run.backward;
    }
    /* The generator must actually reach the interesting transitions. */
    CHECK_STEP(wakes > seeds * 10u && sleeps > seeds * 5u && guarded > seeds && backward > seeds);
    printf("screen model: %u seeds x %u steps, %u double-tap wakes, %u idle sleeps, %u guarded "
           "taps, %u backward steps, %u metamorphic reruns\n",
           seeds, (unsigned)STEPS, wakes, sleeps, guarded, backward, metamorphic);
    return 0;
}

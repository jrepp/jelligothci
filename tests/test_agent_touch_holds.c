#include "jelli/agent_touch.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* Touch samples with holds (ADR-017) under seeded runs: presses that stay still,
 * wobble inside the slop and onto its edges, slide or scrub beyond it, last any time
 * (with jumps onto the exact hold and tap limits), holds switched on and off mid-press
 * as the board does on every sample, and a clock that sometimes runs backwards. A
 * model predicts every event; a second, model-free check reads only the events: per
 * press at most one of TAP, HOLD..RELEASE, or DRAG..STROKE, HOLD at most once,
 * RELEASE exactly once after it, and no TAP after a HOLD or a DRAG. Metamorphic runs
 * replay each script unchanged, shifted in space, shifted in time, and with every
 * sample repeated, and must give the same gestures. Failures print the seed and step. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: seed %u step %u: %s\n", __FILE__, __LINE__, seed, step,        \
                    #expr);                                                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SAMPLES = 1500, SEEDS = 400, CAP = 1000000, CLAMP = 4096, SHIFT = 7919 };

typedef struct {
    bool pressed, holds;
    int x, y;
    uint64_t now;
} Sample;

typedef enum { STILL, WOBBLE, SLIDE, SCRUB } Style;

typedef struct {
    bool down, holds, extreme;
    Style style;
    int x, y, start_x, start_y;
    uint64_t now, start_ms;
} Gen;

typedef enum { IDLE, PENDING, HELD, DRAGGING } Phase;

/* The model: what the sampler should be doing with the current press. */
typedef struct {
    Phase phase;
    int start_x, start_y, last_x, last_y;
    uint64_t start_ms;
    long long path, moved;
    bool clamped, held_off, wandered;
} Model;

/* Model-free: the events of the current press, counted by kind. */
typedef struct {
    unsigned taps, holds, releases, drags, strokes;
} Log;

typedef struct {
    uint64_t all, ends; /* Hashes of every event, and of every gesture's start or end. */
} Digest;

static unsigned seed, step;
static uint32_t state;
static Sample script[SAMPLES], varied[2 * SAMPLES];
static struct {
    unsigned taps, holds, releases, strokes, capped, wandered, held_off, exact_hold, exact_tap,
        late_taps, clamped;
} seen;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static int saturate(long long value)
{
    return value > INT_MAX ? INT_MAX : value < INT_MIN ? INT_MIN : (int)value;
}

static long long clamp(long long value)
{
    return value > CLAMP ? CLAMP : value < -CLAMP ? -CLAMP : value;
}

static bool beyond(int x0, int y0, int x, int y)
{
    long long dx = (long long)x - x0, dy = (long long)y - y0;
    return llabs(dx) > JELLI_AGENT_TAP_SLOP || llabs(dy) > JELLI_AGENT_TAP_SLOP;
}

/* Somewhere to press: on the panel, or (extreme scripts) anywhere an int reaches. */
static int place(bool extreme)
{
    static const int far[] = {INT_MIN,    INT_MIN + 1, -1000000000, 0,
                              1000000000, INT_MAX - 1, INT_MAX};
    if (extreme && next() % 2u == 0u)
        return far[next() % (sizeof(far) / sizeof(far[0]))];
    return (int)(next() % 1000u) - 250;
}

/* Inside the slop, sometimes exactly on its edge, now and then one pixel past it. */
static int wobble(int start)
{
    unsigned roll = next() % 16u;
    long long offset =
        (long long)(next() % (2u * JELLI_AGENT_TAP_SLOP + 1u)) - JELLI_AGENT_TAP_SLOP;
    if (roll == 0u)
        offset = next() % 2u ? JELLI_AGENT_TAP_SLOP : -JELLI_AGENT_TAP_SLOP;
    else if (roll == 1u)
        offset = next() % 2u ? JELLI_AGENT_TAP_SLOP + 1 : -JELLI_AGENT_TAP_SLOP - 1;
    return saturate((long long)start + offset);
}

static void move(Gen *g)
{
    long long jump = 3000 + (long long)(next() % 6000u);
    switch (g->style) {
    case STILL:
        break;
    case WOBBLE:
        g->x = wobble(g->start_x);
        g->y = wobble(g->start_y);
        break;
    case SLIDE:
        g->x = saturate((long long)g->x + (int)(next() % 121u) - 60);
        g->y = saturate((long long)g->y + (int)(next() % 121u) - 60);
        break;
    case SCRUB: /* Far each way in turn: steps the clamp limits, a path toward its cap. */
        g->x = saturate((long long)g->start_x + (g->x > g->start_x ? -jump : jump));
        g->y = saturate((long long)g->start_y + (g->y > g->start_y ? -jump : jump));
        break;
    }
    if (g->style != SCRUB && next() % 24u == 0u)
        g->style = next() % 4u == 0u ? SCRUB : SLIDE; /* Starts to move, or moves while held. */
}

/* Mostly short ticks, sometimes a long gap, onto the hold or tap limits, or backwards. */
static uint64_t advance(const Gen *g)
{
    static const uint64_t limits[] = {JELLI_AGENT_HOLD_MS - 1u, JELLI_AGENT_HOLD_MS,
                                      JELLI_AGENT_HOLD_MS + 1u, JELLI_AGENT_TAP_MAX_MS - 1u,
                                      JELLI_AGENT_TAP_MAX_MS,   JELLI_AGENT_TAP_MAX_MS + 1u};
    unsigned roll = next() % 32u;
    if (roll == 0u)
        return g->now - 1u - next() % 900u; /* Never below the 1000 ms it starts at. */
    if (roll < 4u && g->down)
        return g->start_ms + limits[next() % (sizeof(limits) / sizeof(limits[0]))];
    if (roll == 4u)
        return g->now + 300u + next() % 1500u;
    return g->now + next() % 90u;
}

static Sample generate(Gen *g)
{
    if (next() % 24u == 0u)
        g->holds = !g->holds;
    uint64_t now = advance(g);
    g->now = now < 1000u + 900u ? 1000u + 900u + next() % 50u : now;
    unsigned lift = g->style == SCRUB ? 300u : 12u;
    if ((g->down && next() % lift == 0u) || (!g->down && next() % 4u == 0u)) {
        g->down = false; /* x and y are ignored: anything will do. */
        return (Sample){false, g->holds, (int)next(), (int)next(), g->now};
    }
    if (!g->down) {
        g->down = true;
        g->x = g->start_x = place(g->extreme);
        g->y = g->start_y = place(g->extreme);
        g->start_ms = g->now;
        g->style = (Style)(next() % 3u); /* Still, wobbling, or sliding; scrubs come later. */
    } else {
        move(g);
    }
    return (Sample){true, g->holds, g->x, g->y, g->now};
}

static void write_script(bool extreme)
{
    Gen g = {false, next() % 2u == 0u, extreme, STILL, 0, 0, 0, 0, 1900u, 0u};
    for (unsigned i = 0u; i < SAMPLES; ++i)
        script[i] = generate(&g);
}

/* A press begins: nothing yet. */
static JelliAgentTouchEvent begin(Model *m, const Sample *s)
{
    *m = (Model){PENDING, s->x, s->y, s->x, s->y, s->now, 0, 0, false, false, false};
    return (JelliAgentTouchEvent){JELLI_AGENT_TOUCH_NONE, 0, 0, 0, 0u};
}

static JelliAgentTouchEvent expect_pressed(Model *m, const Sample *s)
{
    JelliAgentTouchEvent none = {JELLI_AGENT_TOUCH_NONE, 0, 0, 0, 0u};
    if (m->phase == IDLE)
        return begin(m, s);
    if (m->phase == HELD) {
        m->wandered |= beyond(m->start_x, m->start_y, s->x, s->y);
        m->held_off |= !s->holds;
        return none;
    }
    if (m->phase == PENDING && !beyond(m->start_x, m->start_y, s->x, s->y)) {
        if (!s->holds || s->now < m->start_ms || s->now - m->start_ms < JELLI_AGENT_HOLD_MS)
            return none;
        m->phase = HELD;
        seen.exact_hold += s->now - m->start_ms == JELLI_AGENT_HOLD_MS;
        return (JelliAgentTouchEvent){JELLI_AGENT_TOUCH_HOLD, m->start_x, m->start_y, 0, 0u};
    }
    long long ddx = (long long)s->x - m->last_x, ddy = (long long)s->y - m->last_y;
    JelliAgentTouchEvent drag = {JELLI_AGENT_TOUCH_DRAG, s->x, s->y, (int)clamp(ddy), 0u};
    m->clamped |= clamp(ddx) != ddx || clamp(ddy) != ddy;
    m->path += llabs(clamp(ddx)) + llabs(clamp(ddy));
    m->path = m->path > CAP ? CAP : m->path;
    m->moved += drag.dy;
    m->last_x = s->x;
    m->last_y = s->y;
    m->phase = DRAGGING;
    return drag;
}

static JelliAgentTouchEvent expect_lifted(Model *m, const Sample *s)
{
    JelliAgentTouchEvent event = {JELLI_AGENT_TOUCH_NONE, m->start_x, m->start_y, 0, 0u};
    if (m->phase == HELD) {
        event.kind = JELLI_AGENT_TOUCH_RELEASE;
        seen.wandered += m->wandered;
        seen.held_off += m->held_off;
    } else if (m->phase == DRAGGING) {
        event.kind = JELLI_AGENT_TOUCH_STROKE;
        event.path = (uint32_t)m->path;
        seen.capped += m->path == CAP;
        seen.clamped += m->clamped;
    } else if (m->phase == PENDING && s->now >= m->start_ms &&
               s->now - m->start_ms <= JELLI_AGENT_TAP_MAX_MS) {
        event.kind = JELLI_AGENT_TOUCH_TAP;
        seen.exact_tap += s->now - m->start_ms == JELLI_AGENT_TAP_MAX_MS;
        /* Held past HOLD_MS with holds on, but lifted before a pressed sample saw it. */
        seen.late_taps += s->holds && s->now - m->start_ms >= JELLI_AGENT_HOLD_MS;
    }
    m->phase = IDLE;
    return event;
}

static void same_event(JelliAgentTouchEvent got, JelliAgentTouchEvent want)
{
    CHECK(got.kind == want.kind);
    if (want.kind == JELLI_AGENT_TOUCH_NONE)
        return; /* Where a NONE points is unspecified. */
    CHECK(got.x == want.x && got.y == want.y);
    CHECK(got.dy == want.dy && got.path == want.path);
}

/* The model-free rules, from the events alone. */
static void log_event(Log *log, bool pressed, JelliAgentTouchKind kind)
{
    if (pressed)
        CHECK(kind == JELLI_AGENT_TOUCH_NONE || kind == JELLI_AGENT_TOUCH_DRAG ||
              kind == JELLI_AGENT_TOUCH_HOLD);
    else
        CHECK(kind == JELLI_AGENT_TOUCH_NONE || kind == JELLI_AGENT_TOUCH_TAP ||
              kind == JELLI_AGENT_TOUCH_RELEASE || kind == JELLI_AGENT_TOUCH_STROKE);
    if (kind == JELLI_AGENT_TOUCH_HOLD)
        CHECK(log->holds == 0u && log->drags == 0u); /* Once, and never after a drag. */
    if (kind == JELLI_AGENT_TOUCH_DRAG)
        CHECK(log->holds == 0u); /* A held finger wanders without dragging. */
    log->taps += kind == JELLI_AGENT_TOUCH_TAP;
    log->holds += kind == JELLI_AGENT_TOUCH_HOLD;
    log->releases += kind == JELLI_AGENT_TOUCH_RELEASE;
    log->drags += kind == JELLI_AGENT_TOUCH_DRAG;
    log->strokes += kind == JELLI_AGENT_TOUCH_STROKE;
    if (pressed)
        return;
    CHECK(log->releases == log->holds && log->holds <= 1u); /* A hold always lets go. */
    CHECK(log->strokes == (log->drags ? 1u : 0u));          /* A drag always strokes. */
    CHECK(log->taps == 0u || (!log->holds && !log->drags)); /* Never a tap after those. */
    CHECK((log->taps != 0u) + (log->holds != 0u) + (log->drags != 0u) <= 1);
    seen.taps += log->taps;
    seen.holds += log->holds;
    seen.releases += log->releases;
    seen.strokes += log->strokes;
    *log = (Log){0u, 0u, 0u, 0u, 0u};
}

static uint64_t mix(uint64_t hash, long long value)
{
    return (hash ^ (uint64_t)value) * 0x100000001b3ull;
}

/* Feeds the samples through a fresh sampler, checking each event against the model
 * and the rules; shift is subtracted from positions before they are hashed. */
static Digest run(const Sample *samples, unsigned count, int shift)
{
    JelliAgentTouch touch;
    Model model = {IDLE, 0, 0, 0, 0, 0u, 0, 0, false, false, false};
    Log log = {0u, 0u, 0u, 0u, 0u};
    Digest digest = {0xcbf29ce484222325ull, 0xcbf29ce484222325ull};
    jelli_agent_touch_init(&touch);
    for (step = 0u; step < count; ++step) {
        const Sample *s = &samples[step];
        jelli_agent_touch_holds(&touch, s->holds);
        JelliAgentTouchEvent got = jelli_agent_touch_sample(&touch, s->pressed, s->x, s->y, s->now);
        same_event(got, s->pressed ? expect_pressed(&model, s) : expect_lifted(&model, s));
        log_event(&log, s->pressed, got.kind);
        if (got.kind == JELLI_AGENT_TOUCH_DRAG && !model.clamped) /* The drags add up. */
            CHECK(model.moved == (long long)s->y - model.start_y);
        if (got.kind == JELLI_AGENT_TOUCH_NONE)
            continue;
        uint64_t h =
            mix(mix(mix(mix(mix(0u, got.kind), (long long)got.x - shift), (long long)got.y - shift),
                    got.dy),
                got.path);
        digest.all = mix(digest.all, (long long)h);
        if (got.kind != JELLI_AGENT_TOUCH_DRAG)
            digest.ends = mix(digest.ends, (long long)h);
    }
    return digest;
}

static void transform(int shift, uint64_t later, bool twice)
{
    unsigned n = 0u;
    for (unsigned i = 0u; i < SAMPLES; ++i) {
        Sample s = script[i];
        s.x = s.pressed ? s.x + shift : s.x;
        s.y = s.pressed ? s.y + shift : s.y;
        s.now += later;
        varied[n++] = s;
        if (twice)
            varied[n++] = s;
    }
}

/* Same script, same events; moved in space or time, or sampled twice as often with
 * every sample repeated, the same gestures where they began. */
static void metamorphic(void)
{
    Digest base = run(script, SAMPLES, 0), again = run(script, SAMPLES, 0);
    CHECK(base.all == again.all && base.ends == again.ends);
    transform(SHIFT, 0u, false);
    Digest moved = run(varied, SAMPLES, SHIFT);
    CHECK(moved.all == base.all);
    transform(0, 1000000000000ull, false);
    Digest later = run(varied, SAMPLES, 0);
    CHECK(later.all == base.all);
    transform(0, 0u, true);
    Digest doubled = run(varied, 2u * SAMPLES, 0);
    CHECK(doubled.ends == base.ends);
}

int main(void)
{
    for (seed = 0u; seed < SEEDS; ++seed) {
        state = 0x9e3779b9u ^ (seed * 2654435761u) ^ 1u;
        write_script(false);
        metamorphic();
        write_script(true); /* Coordinates at int's limits: the model alone, no shifts. */
        (void)run(script, SAMPLES, 0);
    }
    step = 0u;
    CHECK(seen.taps > 1000u && seen.holds > 1000u && seen.strokes > 1000u);
    CHECK(seen.releases == seen.holds);
    CHECK(seen.capped > 0u && seen.clamped > 0u && seen.wandered > 0u && seen.held_off > 0u);
    CHECK(seen.exact_hold > 0u && seen.exact_tap > 0u);
    printf("Touch hold properties passed (%u seeds x 5 runs; %u taps, %u holds, %u strokes, "
           "%u capped, %u holds wandered, %u released after holds went off, %u late taps)\n",
           (unsigned)SEEDS, seen.taps, seen.holds, seen.strokes, seen.capped, seen.wandered,
           seen.held_off, seen.late_taps);
    return 0;
}

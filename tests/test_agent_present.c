/* The agent's panel windows (core/agent/present.c): examples, then every span on the
 * 466-pixel axes against properties (even edges, minimal cover, inside the surface,
 * stripe never overrun), then seeded rectangles with huge fields, odd surfaces and
 * random stripe sizes. A failure prints its rectangle so it can be pinned. */
#include "jelli/agent_present.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* Production geometry: the 466x466 panel and a 16-row full-width stripe. */
enum { SIDE = JELLI_WIDTH, STRIPE = SIDE * 16 };
static const unsigned untouched = 0xdeadbeefu;

/* Reports the case before failing, so it can be pinned as a regression example. */
static void fail(const char *what, JelliRect r, unsigned width, unsigned height, size_t stripe)
{
    fprintf(stderr, "present window: %s for rect {%u, %u, %u, %u} on %ux%u, stripe %zu\n", what,
            r.x, r.y, r.width, r.height, width, height, stripe);
    exit(1);
}

/* [start, start + length) clipped to [0, limit), in 64 bits. */
static void clip_span(unsigned start, unsigned length, unsigned limit, uint64_t *low,
                      uint64_t *high)
{
    uint64_t end = (uint64_t)start + length;
    *low = start;
    *high = end > limit ? limit : end;
}

/* Walks the sends exactly as the ESP32 present loop does; each one is even-aligned,
 * fits the stripe, and together they tile [y0, y1) with no gap or overlap. */
static bool sends_tile(const JelliAgentWindow *w, size_t stripe)
{
    unsigned width = w->x1 - w->x0, y = w->y0, rows, sends = 0u;
    while ((rows = jelli_agent_window_rows(w, y)) != 0u) {
        if (y % 2u || rows % 2u || rows > w->rows_per_send || y + rows > w->y1 ||
            (size_t)rows * width > stripe)
            return false;
        y += rows;
        ++sends;
    }
    unsigned height = w->y1 - w->y0;
    return y == w->y1 && sends == (height + w->rows_per_send - 1u) / w->rows_per_send;
}

/* Even edges, covering the clipped damage by less than one pixel per side, inside. */
static bool edges_hold(const JelliAgentWindow *w, uint64_t cx0, uint64_t cx1, uint64_t cy0,
                       uint64_t cy1, unsigned width, unsigned height)
{
    bool even = !(w->x0 % 2u || w->y0 % 2u || w->x1 % 2u || w->y1 % 2u);
    bool covers = w->x0 <= cx0 && w->y0 <= cy0 && w->x1 >= cx1 && w->y1 >= cy1;
    bool minimal = cx0 < w->x0 + 2u && cy0 < w->y0 + 2u && w->x1 < cx1 + 2u && w->y1 < cy1 + 2u;
    bool inside = w->x1 <= width && w->y1 <= height && w->x0 < w->x1 && w->y0 < w->y1;
    return even && covers && minimal && inside;
}

/* Even rows per send that fit the stripe, as many as fit or the whole window. */
static bool rows_hold(const JelliAgentWindow *w, size_t stripe)
{
    size_t columns = w->x1 - w->x0;
    bool fits = w->rows_per_send >= 2u && w->rows_per_send % 2u == 0u &&
                (size_t)w->rows_per_send * columns <= stripe;
    bool most = w->rows_per_send == w->y1 - w->y0 || (w->rows_per_send + 2u) * columns > stripe;
    return fits && most && sends_tile(w, stripe);
}

/* Checks one case against the properties; the result says whether a window came back. */
static bool check(JelliRect r, unsigned width, unsigned height, size_t stripe)
{
    JelliAgentWindow w = {untouched, untouched, untouched, untouched, untouched};
    bool ok = jelli_agent_present_window(r, width, height, stripe, &w);
    uint64_t cx0, cx1, cy0, cy1;
    clip_span(r.x, r.width, width, &cx0, &cx1);
    clip_span(r.y, r.height, height, &cy0, &cy1);
    bool valid = width && height && width % 2u == 0u && height % 2u == 0u;
    bool damaged = cx0 < cx1 && cy0 < cy1;
    /* The narrowest even-edged window over the damage, and whether two rows of it fit. */
    uint64_t narrowest = damaged ? ((cx1 + 1u) & ~(uint64_t)1u) - (cx0 & ~(uint64_t)1u) : 0u;
    bool expect = valid && damaged && stripe / narrowest >= 2u;
    if (ok != expect)
        fail(ok ? "unexpected window" : "missing window", r, width, height, stripe);
    if (!ok && (w.x0 != untouched || w.rows_per_send != untouched))
        fail("window written on failure", r, width, height, stripe);
    if (ok && !(edges_hold(&w, cx0, cx1, cy0, cy1, width, height) && rows_hold(&w, stripe)))
        fail("property violated", r, width, height, stripe);
    return ok;
}

static void examples(void)
{
    JelliAgentWindow w;
    CHECK(jelli_agent_present_window((JelliRect){0u, 0u, SIDE, SIDE}, SIDE, SIDE, STRIPE, &w));
    CHECK(w.x0 == 0u && w.y0 == 0u && w.x1 == SIDE && w.y1 == SIDE && w.rows_per_send == 16u);
    CHECK(jelli_agent_window_rows(&w, 464u) == 2u && jelli_agent_window_rows(&w, SIDE) == 0u);
    CHECK(jelli_agent_present_window((JelliRect){465u, 465u, 1u, 1u}, SIDE, SIDE, STRIPE, &w));
    CHECK(w.x0 == 464u && w.y0 == 464u && w.x1 == SIDE && w.y1 == SIDE && w.rows_per_send == 2u);
    CHECK(jelli_agent_present_window((JelliRect){101u, 7u, 100u, 1u}, SIDE, SIDE, STRIPE, &w));
    CHECK(w.x0 == 100u && w.x1 == 202u && w.y0 == 6u && w.y1 == 8u);
    /* 102 columns: 7456 / 102 = 73 rows, rounded down to even. */
    CHECK(jelli_agent_present_window((JelliRect){101u, 0u, 100u, SIDE}, SIDE, SIDE, STRIPE, &w));
    CHECK(w.rows_per_send == 72u);
    /* Regression: the original 32-bit end wrapped to 0 and dropped this damage. */
    CHECK(jelli_agent_present_window((JelliRect){1u, 3u, UINT_MAX, UINT_MAX}, SIDE, SIDE, STRIPE,
                                     &w));
    CHECK(w.x0 == 0u && w.x1 == SIDE && w.y0 == 2u && w.y1 == SIDE);
    CHECK(!jelli_agent_present_window((JelliRect){SIDE, 0u, 4u, 4u}, SIDE, SIDE, STRIPE, &w));
    CHECK(!jelli_agent_present_window((JelliRect){4u, 4u, 0u, 9u}, SIDE, SIDE, STRIPE, &w));
    CHECK(!jelli_agent_present_window((JelliRect){0u, 0u, 4u, 4u}, 465u, SIDE, STRIPE, &w));
    CHECK(!jelli_agent_present_window((JelliRect){0u, 0u, 4u, 4u}, SIDE, 0u, STRIPE, &w));
    CHECK(!jelli_agent_present_window((JelliRect){0u, 0u, 4u, 4u}, SIDE, SIDE, 7u, &w));
    CHECK(jelli_agent_present_window((JelliRect){0u, 0u, 4u, 4u}, SIDE, SIDE, 8u, &w));
    CHECK(w.rows_per_send == 2u);
}

/* Every start 0..468 and length 0..470 on one axis (the other full or a single row),
 * plus lengths that overflow 32 bits. Axes are independent apart from the window width,
 * which the x sweep covers for every even value. */
static void every_span(void)
{
    static const unsigned huge[] = {UINT_MAX, UINT_MAX - 1u, 0x80000000u, UINT_MAX - 465u};
    unsigned windows = 0u;
    for (unsigned start = 0u; start <= SIDE + 2u; ++start) {
        for (unsigned length = 0u; length <= SIDE + 4u + 4u; ++length) {
            unsigned l = length <= SIDE + 4u ? length : huge[length - SIDE - 5u];
            windows += check((JelliRect){start, 0u, l, SIDE}, SIDE, SIDE, STRIPE);
            windows += check((JelliRect){start, 233u, l, 1u}, SIDE, SIDE, STRIPE);
            windows += check((JelliRect){0u, start, SIDE, l}, SIDE, SIDE, STRIPE);
            windows += check((JelliRect){465u, start, 1u, l}, SIDE, SIDE, STRIPE);
        }
    }
    CHECK(windows > 400000u); /* The sweep really produced windows, not only refusals. */
}

/* Every damage rectangle whose corners lie on a coarse grid, both edges free. */
static void grid_rectangles(void)
{
    for (unsigned x = 0u; x <= SIDE; x += 7u)
        for (unsigned y = 0u; y <= SIDE; y += 11u)
            for (unsigned w = 0u; x + w <= SIDE + 3u; w += 5u)
                for (unsigned h = 0u; y + h <= SIDE + 3u; h += 13u)
                    check((JelliRect){x, y, w, h}, SIDE, SIDE, STRIPE);
}

static uint32_t next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Near the surface most of the time; otherwise 0, 2^31 or near UINT_MAX. */
static unsigned field(uint32_t *state, unsigned extent)
{
    unsigned roll = next(state) % 8u;
    if (roll == 0u)
        return UINT_MAX - next(state) % 512u;
    if (roll == 1u)
        return 0x80000000u + next(state) % 512u - 256u;
    if (roll == 2u)
        return 0u;
    return next(state) % (extent + 8u);
}

static void seeded_rectangles(void)
{
    static const size_t stripes[] = {0u, 1u, 2u, 3u, 4u, 5u, 931u, 932u, STRIPE, SIZE_MAX};
    for (uint32_t seed = 1u; seed <= 300000u; ++seed) {
        uint32_t state = seed * 2654435761u | 1u;
        unsigned width = next(&state) % 4u ? SIDE : next(&state) % 600u;
        unsigned height = next(&state) % 4u ? SIDE : next(&state) % 600u;
        size_t stripe = next(&state) % 3u ? stripes[next(&state) % 10u] : next(&state) % 20000u;
        check((JelliRect){field(&state, width), field(&state, height), field(&state, width),
                          field(&state, height)},
              width, height, stripe);
    }
}

int main(void)
{
    examples();
    every_span();
    grid_rectangles();
    seeded_rectangles();
    puts("Agent present window checks passed");
    return 0;
}

#include "jelli/agent_touch.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* The edges of the touch gestures (ADR-016, ADR-017): the hold and tap limits to the
 * millisecond with holds on and off, the slop's edge, what a hold, a drag, and a
 * stroke carry, the distance's clamp and cap, and holds switched mid-press. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum {
    SLOP = JELLI_AGENT_TAP_SLOP,
    HOLD_MS = JELLI_AGENT_HOLD_MS,
    TAP_MS = JELLI_AGENT_TAP_MAX_MS
};

static JelliAgentTouch fresh(bool holds)
{
    JelliAgentTouch touch;
    jelli_agent_touch_init(&touch);
    jelli_agent_touch_holds(&touch, holds);
    return touch;
}

static JelliAgentTouchKind at(JelliAgentTouch *touch, bool pressed, int x, int y, uint64_t ms)
{
    return jelli_agent_touch_sample(touch, pressed, x, y, ms).kind;
}

/* With holds on, a still sample at HOLD_MS - 1 is nothing and one at HOLD_MS holds;
 * with holds off neither does, and a slow press taps up to TAP_MS. */
static void test_the_limits_to_the_millisecond(void)
{
    for (int holds = 0; holds < 2; ++holds) {
        JelliAgentTouch touch = fresh(holds != 0);
        (void)at(&touch, true, 50, 60, 1000u);
        CHECK(at(&touch, true, 50, 60, 1000u + HOLD_MS - 1u) == JELLI_AGENT_TOUCH_NONE);
        CHECK(at(&touch, true, 50, 60, 1000u + HOLD_MS) ==
              (holds ? JELLI_AGENT_TOUCH_HOLD : JELLI_AGENT_TOUCH_NONE));
        CHECK(at(&touch, true, 50, 60, 1000u + HOLD_MS + 1u) == JELLI_AGENT_TOUCH_NONE);
        CHECK(at(&touch, false, 0, 0, 1000u + TAP_MS + 5000u) ==
              (holds ? JELLI_AGENT_TOUCH_RELEASE : JELLI_AGENT_TOUCH_NONE));
        /* Lifted just before the hold limit, or the very moment it began: a tap. */
        (void)at(&touch, true, 50, 60, 9000u);
        CHECK(at(&touch, false, 0, 0, 9000u + HOLD_MS - 1u) == JELLI_AGENT_TOUCH_TAP);
        (void)at(&touch, true, 50, 60, 10000u);
        CHECK(at(&touch, false, 0, 0, 10000u) == JELLI_AGENT_TOUCH_TAP);
    }
    /* Without holds, still samples past the hold limit, lifted at the tap limit and one
     * past it. */
    JelliAgentTouch touch = fresh(false);
    (void)at(&touch, true, 50, 60, 9000u);
    CHECK(at(&touch, true, 50, 60, 9000u + TAP_MS - 1u) == JELLI_AGENT_TOUCH_NONE);
    CHECK(at(&touch, false, 0, 0, 9000u + TAP_MS) == JELLI_AGENT_TOUCH_TAP);
    (void)at(&touch, true, 50, 60, 12000u);
    CHECK(at(&touch, true, 50, 60, 12000u + TAP_MS) == JELLI_AGENT_TOUCH_NONE);
    CHECK(at(&touch, false, 0, 0, 12000u + TAP_MS + 1u) == JELLI_AGENT_TOUCH_NONE);
}

/* A hold is decided by a pressed sample. A press lifted after HOLD_MS but before any
 * pressed sample saw it there is a TAP, even with holds on (reported: the header says
 * such a press "is a hold instead"; harmless on the board, which samples every 40 ms
 * and ignores taps on the unicorn, but a stalled sampler would turn a hold into a tap). */
static void test_a_hold_needs_a_sample_to_see_it(void)
{
    JelliAgentTouch touch = fresh(true);
    (void)at(&touch, true, 10, 10, 0u);
    CHECK(at(&touch, false, 0, 0, HOLD_MS) == JELLI_AGENT_TOUCH_TAP);
    (void)at(&touch, true, 10, 10, 2000u);
    CHECK(at(&touch, true, 10, 10, 2000u + HOLD_MS - 1u) == JELLI_AGENT_TOUCH_NONE);
    CHECK(at(&touch, false, 0, 0, 2000u + HOLD_MS + 1u) == JELLI_AGENT_TOUCH_TAP);
}

/* On the slop's edge, either axis or both, it is still in place and holds; one pixel
 * past it on one axis is a drag that never holds, even once it comes back. */
static void test_the_slop_edge_for_holds(void)
{
    static const int edges[][2] = {{SLOP, 0},    {-SLOP, 0},     {0, SLOP},    {0, -SLOP},
                                   {SLOP, SLOP}, {-SLOP, -SLOP}, {SLOP, -SLOP}};
    for (size_t i = 0u; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        JelliAgentTouch touch = fresh(true);
        (void)at(&touch, true, 200, 200, 0u);
        JelliAgentTouchEvent hold =
            jelli_agent_touch_sample(&touch, true, 200 + edges[i][0], 200 + edges[i][1], HOLD_MS);
        CHECK(hold.kind == JELLI_AGENT_TOUCH_HOLD && hold.x == 200 && hold.y == 200);
        CHECK(hold.dy == 0 && hold.path == 0u);
        CHECK(at(&touch, false, 0, 0, HOLD_MS + 1u) == JELLI_AGENT_TOUCH_RELEASE);
    }
    JelliAgentTouch touch = fresh(true);
    (void)at(&touch, true, 200, 200, 0u);
    JelliAgentTouchEvent drag =
        jelli_agent_touch_sample(&touch, true, 200 - SLOP - 1, 200, HOLD_MS);
    CHECK(drag.kind == JELLI_AGENT_TOUCH_DRAG && drag.dy == 0 && drag.path == 0u);
    CHECK(drag.x == 200 - SLOP - 1 && drag.y == 200);
    CHECK(at(&touch, true, 200, 200, (uint64_t)HOLD_MS * 2u) ==
          JELLI_AGENT_TOUCH_DRAG); /* Back: no hold. */
    JelliAgentTouchEvent stroke = jelli_agent_touch_sample(&touch, false, 0, 0, 5000u);
    CHECK(stroke.kind == JELLI_AGENT_TOUCH_STROKE && stroke.x == 200 && stroke.y == 200);
    CHECK(stroke.path == 2u * (SLOP + 1u) && stroke.dy == 0); /* Out and back. */
}

/* Once held, the finger may go anywhere, even to int's limits; the release is where
 * the press began, carries nothing else, and comes whatever the clock says. */
static void test_a_hold_that_wanders_still_releases_where_it_began(void)
{
    JelliAgentTouch touch = fresh(true);
    (void)at(&touch, true, INT_MAX, INT_MIN, 100u);
    CHECK(at(&touch, true, INT_MAX, INT_MIN, 100u + HOLD_MS) == JELLI_AGENT_TOUCH_HOLD);
    CHECK(at(&touch, true, INT_MIN, INT_MAX, 100u + HOLD_MS + 1u) == JELLI_AGENT_TOUCH_NONE);
    CHECK(at(&touch, true, 0, 0, 3u) == JELLI_AGENT_TOUCH_NONE); /* Clock went back. */
    JelliAgentTouchEvent release = jelli_agent_touch_sample(&touch, false, 7, 7, 1u);
    CHECK(release.kind == JELLI_AGENT_TOUCH_RELEASE);
    CHECK(release.x == INT_MAX && release.y == INT_MIN && release.dy == 0 && release.path == 0u);
    CHECK(at(&touch, false, 7, 7, 2u) == JELLI_AGENT_TOUCH_NONE);
}

/* Holds switched on after HOLD_MS hold on the next still sample; switched off before,
 * the press stays a slow tap up to TAP_MS; switching them on during a drag changes
 * nothing; and init turns them off again. */
static void test_holds_switched_mid_press(void)
{
    JelliAgentTouch touch = fresh(false);
    (void)at(&touch, true, 30, 30, 0u);
    CHECK(at(&touch, true, 30, 30, HOLD_MS + 200u) == JELLI_AGENT_TOUCH_NONE);
    jelli_agent_touch_holds(&touch, true);
    CHECK(at(&touch, true, 30, 30, HOLD_MS + 201u) == JELLI_AGENT_TOUCH_HOLD);
    CHECK(at(&touch, false, 0, 0, HOLD_MS + 202u) == JELLI_AGENT_TOUCH_RELEASE);

    (void)at(&touch, true, 30, 30, 1000u);
    CHECK(at(&touch, true, 30, 30, 1000u + HOLD_MS - 1u) == JELLI_AGENT_TOUCH_NONE);
    jelli_agent_touch_holds(&touch, false);
    CHECK(at(&touch, true, 30, 30, 1000u + TAP_MS - 1u) == JELLI_AGENT_TOUCH_NONE);
    CHECK(at(&touch, false, 0, 0, 1000u + TAP_MS) == JELLI_AGENT_TOUCH_TAP);

    jelli_agent_touch_holds(&touch, false);
    (void)at(&touch, true, 30, 30, 5000u);
    CHECK(at(&touch, true, 90, 30, 5010u) == JELLI_AGENT_TOUCH_DRAG);
    jelli_agent_touch_holds(&touch, true);
    CHECK(at(&touch, true, 30, 30, 5000u + HOLD_MS) == JELLI_AGENT_TOUCH_DRAG);
    CHECK(at(&touch, false, 0, 0, 9000u) == JELLI_AGENT_TOUCH_STROKE);

    jelli_agent_touch_init(&touch); /* Holds off again. */
    (void)at(&touch, true, 30, 30, 10000u);
    CHECK(at(&touch, true, 30, 30, 10000u + HOLD_MS) == JELLI_AGENT_TOUCH_NONE);
}

/* A new press after a release or a stroke starts afresh where it lands: a new hold
 * there, keeping the holds setting, and a stroke counting only its own distance. */
static void test_a_press_after_a_gesture_starts_afresh(void)
{
    JelliAgentTouch touch = fresh(true);
    (void)at(&touch, true, 10, 10, 0u);
    CHECK(at(&touch, true, 10, 10, HOLD_MS) == JELLI_AGENT_TOUCH_HOLD);
    CHECK(at(&touch, false, 0, 0, HOLD_MS + 1u) == JELLI_AGENT_TOUCH_RELEASE);
    (void)at(&touch, true, 300, 310, 1000u);
    JelliAgentTouchEvent hold = jelli_agent_touch_sample(&touch, true, 300, 310, 1000u + HOLD_MS);
    CHECK(hold.kind == JELLI_AGENT_TOUCH_HOLD && hold.x == 300 && hold.y == 310);
    CHECK(at(&touch, false, 0, 0, 2000u) == JELLI_AGENT_TOUCH_RELEASE);

    (void)at(&touch, true, 0, 0, 3000u);
    (void)at(&touch, true, 100, 0, 3010u);
    CHECK(jelli_agent_touch_sample(&touch, false, 0, 0, 3020u).path == 100u);
    (void)at(&touch, true, 500, 500, 4000u);
    JelliAgentTouchEvent first = jelli_agent_touch_sample(&touch, true, 500, 470, 4010u);
    CHECK(first.kind == JELLI_AGENT_TOUCH_DRAG && first.dy == -30);
    JelliAgentTouchEvent stroke = jelli_agent_touch_sample(&touch, false, 0, 0, 4020u);
    CHECK(stroke.kind == JELLI_AGENT_TOUCH_STROKE && stroke.x == 500 && stroke.y == 500);
    CHECK(stroke.path == 30u);               /* Not 130: the last stroke's distance is gone. */
    (void)at(&touch, true, 500, 500, 5000u); /* And a quick still press taps again. */
    CHECK(at(&touch, false, 0, 0, 5001u) == JELLI_AGENT_TOUCH_TAP);
}

/* The distance is counted from where the press began once it drags; wobbles inside
 * the slop before that are not counted. A drag has no time limit or clock rule. */
static void test_what_a_stroke_counts(void)
{
    JelliAgentTouch touch = fresh(true);
    (void)at(&touch, true, 0, 0, 0u);
    CHECK(at(&touch, true, SLOP, -SLOP, 10u) == JELLI_AGENT_TOUCH_NONE);
    CHECK(at(&touch, true, -SLOP, SLOP, 20u) == JELLI_AGENT_TOUCH_NONE);
    JelliAgentTouchEvent drag = jelli_agent_touch_sample(&touch, true, SLOP + 6, 3, 30u);
    CHECK(drag.kind == JELLI_AGENT_TOUCH_DRAG && drag.dy == 3 && drag.path == 0u);
    CHECK(at(&touch, true, SLOP + 6, 3, 50000u) == JELLI_AGENT_TOUCH_DRAG); /* Still: dy 0. */
    JelliAgentTouchEvent stroke = jelli_agent_touch_sample(&touch, false, 0, 0, 5u);
    CHECK(stroke.kind == JELLI_AGENT_TOUCH_STROKE && stroke.path == SLOP + 6u + 3u);
    /* Each axis of each step is clamped to 4096 before it is counted. */
    (void)at(&touch, true, 0, 0, 100u);
    drag = jelli_agent_touch_sample(&touch, true, 100000, -100000, 110u);
    CHECK(drag.kind == JELLI_AGENT_TOUCH_DRAG && drag.dy == -4096);
    CHECK(jelli_agent_touch_sample(&touch, false, 0, 0, 120u).path == 8192u);
}

/* The cap is reached exactly and then held, never wrapped: 122 steps of 8192 is
 * 999424, 575 more is one short, one more is the cap, and more stays there. */
static void test_the_distance_cap_to_the_pixel(void)
{
    JelliAgentTouch touch = fresh(false);
    (void)at(&touch, true, 0, 0, 0u);
    for (uint64_t i = 1u; i <= 122u; ++i) {
        int far = (i % 2u) ? 4096 : 0;
        CHECK(at(&touch, true, far, far, i) == JELLI_AGENT_TOUCH_DRAG);
    }
    CHECK(at(&touch, true, 575, 0, 200u) == JELLI_AGENT_TOUCH_DRAG);
    JelliAgentTouch below = touch; /* 999999 so far. */
    CHECK(jelli_agent_touch_sample(&below, false, 0, 0, 201u).path == 999999u);
    (void)at(&touch, true, 576, 0, 202u);
    JelliAgentTouch exact = touch;
    CHECK(jelli_agent_touch_sample(&exact, false, 0, 0, 203u).path == 1000000u);
    (void)at(&touch, true, 577, 0, 204u);
    (void)at(&touch, true, -4096, 4096, 205u);
    CHECK(jelli_agent_touch_sample(&touch, false, 0, 0, 206u).path == 1000000u);
}

int main(void)
{
    test_the_limits_to_the_millisecond();
    test_a_hold_needs_a_sample_to_see_it();
    test_the_slop_edge_for_holds();
    test_a_hold_that_wanders_still_releases_where_it_began();
    test_holds_switched_mid_press();
    test_a_press_after_a_gesture_starts_afresh();
    test_what_a_stroke_counts();
    test_the_distance_cap_to_the_pixel();
    printf("Touch boundary checks passed\n");
    return 0;
}

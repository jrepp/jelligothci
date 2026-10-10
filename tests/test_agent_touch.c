#include "jelli/agent_touch.h"
#include <stdio.h>
#include <stdlib.h>

/* Touch samples become taps, vertical drags, strokes, and holds (ADR-016, ADR-017). */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static JelliAgentTouchEvent at(JelliAgentTouch *touch, bool pressed, int x, int y, uint64_t ms)
{
    return jelli_agent_touch_sample(touch, pressed, x, y, ms);
}

static void test_a_short_press_in_place_is_a_tap_where_it_began(void)
{
    JelliAgentTouch touch;
    jelli_agent_touch_init(&touch);
    CHECK(at(&touch, false, 0, 0, 0u).kind == JELLI_AGENT_TOUCH_NONE); /* Nothing pressed yet. */
    CHECK(at(&touch, true, 100, 200, 10u).kind == JELLI_AGENT_TOUCH_NONE);
    CHECK(at(&touch, true, 100 + JELLI_AGENT_TAP_SLOP, 200 - JELLI_AGENT_TAP_SLOP, 50u).kind ==
          JELLI_AGENT_TOUCH_NONE);
    JelliAgentTouchEvent tap = at(&touch, false, 999, 999, 80u);
    CHECK(tap.kind == JELLI_AGENT_TOUCH_TAP && tap.x == 100 && tap.y == 200);
    CHECK(at(&touch, false, 0, 0, 90u).kind == JELLI_AGENT_TOUCH_NONE); /* Only once. */
}

static void test_a_long_press_is_no_tap(void)
{
    JelliAgentTouch touch;
    jelli_agent_touch_init(&touch);
    (void)at(&touch, true, 5, 5, 1000u);
    CHECK(at(&touch, false, 5, 5, 1000u + JELLI_AGENT_TAP_MAX_MS + 1u).kind ==
          JELLI_AGENT_TOUCH_NONE);
    (void)at(&touch, true, 5, 5, 3000u);
    CHECK(at(&touch, false, 5, 5, 3000u + JELLI_AGENT_TAP_MAX_MS).kind == JELLI_AGENT_TOUCH_TAP);
    (void)at(&touch, true, 5, 5, 5000u); /* A clock running backwards is no tap. */
    CHECK(at(&touch, false, 5, 5, 4000u).kind == JELLI_AGENT_TOUCH_NONE);
}

static void test_moving_beyond_the_slop_drags_and_never_taps(void)
{
    JelliAgentTouch touch;
    jelli_agent_touch_init(&touch);
    (void)at(&touch, true, 200, 300, 0u);
    JelliAgentTouchEvent first = at(&touch, true, 200, 300 - JELLI_AGENT_TAP_SLOP - 1, 20u);
    CHECK(first.kind == JELLI_AGENT_TOUCH_DRAG && first.dy == -JELLI_AGENT_TAP_SLOP - 1);
    JelliAgentTouchEvent next = at(&touch, true, 260, 250, 40u);
    CHECK(next.kind == JELLI_AGENT_TOUCH_DRAG && next.dy == 250 - (300 - JELLI_AGENT_TAP_SLOP - 1));
    CHECK(at(&touch, true, 200, 300, 60u).dy == 50); /* Back where it began: still a drag. */
    /* Lifting ends it as a stroke: 15 up, then 60 across and 35 up, then 60 and 50 back. */
    JelliAgentTouchEvent stroke = at(&touch, false, 200, 300, 70u);
    CHECK(stroke.kind == JELLI_AGENT_TOUCH_STROKE && stroke.x == 200 && stroke.y == 300);
    CHECK(stroke.path == 15u + 60u + 35u + 60u + 50u);
    /* Sideways too far is a drag of no height, and so no tap either. */
    (void)at(&touch, true, 10, 10, 100u);
    JelliAgentTouchEvent sideways = at(&touch, true, 40, 10, 120u);
    CHECK(sideways.kind == JELLI_AGENT_TOUCH_DRAG && sideways.dy == 0);
    CHECK(at(&touch, false, 0, 0, 130u).kind == JELLI_AGENT_TOUCH_STROKE);
}

/* Where holds are allowed, a still press becomes a hold at JELLI_AGENT_HOLD_MS, then
 * releases however far the finger wanders; it is never a tap. */
static void test_a_still_press_is_a_hold_where_allowed(void)
{
    JelliAgentTouch touch;
    jelli_agent_touch_init(&touch);
    jelli_agent_touch_holds(&touch, true);
    (void)at(&touch, true, 100, 100, 1000u);
    CHECK(at(&touch, true, 105, 95, 1000u + JELLI_AGENT_HOLD_MS - 1u).kind ==
          JELLI_AGENT_TOUCH_NONE);
    JelliAgentTouchEvent hold = at(&touch, true, 105, 95, 1000u + JELLI_AGENT_HOLD_MS);
    CHECK(hold.kind == JELLI_AGENT_TOUCH_HOLD && hold.x == 100 && hold.y == 100);
    CHECK(at(&touch, true, 300, 400, 1500u).kind == JELLI_AGENT_TOUCH_NONE); /* Still held. */
    CHECK(at(&touch, false, 0, 0, 9000u).kind == JELLI_AGENT_TOUCH_RELEASE);
    CHECK(at(&touch, false, 0, 0, 9010u).kind == JELLI_AGENT_TOUCH_NONE); /* Only once. */
    /* A short press still taps; one that moves first is a drag, never a hold. */
    (void)at(&touch, true, 50, 50, 10000u);
    CHECK(at(&touch, false, 50, 50, 10000u + JELLI_AGENT_HOLD_MS - 1u).kind ==
          JELLI_AGENT_TOUCH_TAP);
    (void)at(&touch, true, 50, 50, 11000u);
    CHECK(at(&touch, true, 90, 50, 11100u).kind == JELLI_AGENT_TOUCH_DRAG);
    CHECK(at(&touch, true, 90, 50, 13000u).kind == JELLI_AGENT_TOUCH_DRAG);
    CHECK(at(&touch, false, 90, 50, 13010u).kind == JELLI_AGENT_TOUCH_STROKE);
}

/* Without holds (Settings), a slow press is still a tap up to JELLI_AGENT_TAP_MAX_MS;
 * turning holds off during a hold still ends it with a release. */
static void test_without_holds_a_slow_press_taps(void)
{
    JelliAgentTouch touch;
    jelli_agent_touch_init(&touch);
    (void)at(&touch, true, 20, 20, 0u);
    CHECK(at(&touch, true, 20, 20, JELLI_AGENT_HOLD_MS + 100u).kind == JELLI_AGENT_TOUCH_NONE);
    CHECK(at(&touch, false, 20, 20, JELLI_AGENT_HOLD_MS + 200u).kind == JELLI_AGENT_TOUCH_TAP);
    jelli_agent_touch_holds(&touch, true);
    (void)at(&touch, true, 20, 20, 1000u);
    CHECK(at(&touch, true, 20, 20, 1000u + JELLI_AGENT_HOLD_MS).kind == JELLI_AGENT_TOUCH_HOLD);
    jelli_agent_touch_holds(&touch, false);
    CHECK(at(&touch, false, 20, 20, 2000u).kind == JELLI_AGENT_TOUCH_RELEASE);
    (void)at(&touch, true, 20, 20, 3000u); /* And no new hold while they stay off. */
    CHECK(at(&touch, true, 20, 20, 3000u + JELLI_AGENT_HOLD_MS).kind == JELLI_AGENT_TOUCH_NONE);
    /* A clock running backwards never makes a hold. */
    jelli_agent_touch_holds(&touch, true);
    (void)at(&touch, false, 0, 0, 4000u);
    (void)at(&touch, true, 20, 20, 5000u);
    CHECK(at(&touch, true, 20, 20, 100u).kind == JELLI_AGENT_TOUCH_NONE);
}

static void test_extreme_coordinates_are_safe(void)
{
    JelliAgentTouch touch;
    jelli_agent_touch_init(&touch);
    (void)at(&touch, true, -2147483647 - 1, -2147483647 - 1, 0u);
    JelliAgentTouchEvent jump = at(&touch, true, 2147483647, 2147483647, 1u);
    CHECK(jump.kind == JELLI_AGENT_TOUCH_DRAG && jump.dy == 4096);
    CHECK(at(&touch, true, 2147483647, -2147483647 - 1, 2u).dy == -4096);
    CHECK(jelli_agent_touch_sample(NULL, true, 0, 0, 0u).kind == JELLI_AGENT_TOUCH_NONE);
    jelli_agent_touch_init(NULL);
    jelli_agent_touch_holds(NULL, true);
    /* The distance travelled stops at its cap instead of wrapping. */
    JelliAgentTouchEvent last = jelli_agent_touch_sample(&touch, false, 0, 0, 3u);
    CHECK(last.kind == JELLI_AGENT_TOUCH_STROKE && last.path == 4096u + 4096u + 4096u);
    (void)at(&touch, true, 0, 0, 10u);
    for (uint64_t ms = 11u; ms < 2000u; ++ms)
        (void)at(&touch, true, (ms % 2u) ? 4096 : -4096, 0, ms);
    CHECK(at(&touch, false, 0, 0, 2000u).path == 1000000u);
}

int main(void)
{
    test_a_short_press_in_place_is_a_tap_where_it_began();
    test_a_long_press_is_no_tap();
    test_moving_beyond_the_slop_drags_and_never_taps();
    test_a_still_press_is_a_hold_where_allowed();
    test_without_holds_a_slow_press_taps();
    test_extreme_coordinates_are_safe();
    printf("Touch checks passed\n");
    return 0;
}

#include "jelli/agent_touch.h"
#include <stdio.h>
#include <stdlib.h>

/* Touch samples become taps and vertical drags (ADR-016). */

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
    CHECK(at(&touch, false, 200, 300, 70u).kind == JELLI_AGENT_TOUCH_NONE);
    /* Sideways too far is a drag of no height, and so no tap either. */
    (void)at(&touch, true, 10, 10, 100u);
    JelliAgentTouchEvent sideways = at(&touch, true, 40, 10, 120u);
    CHECK(sideways.kind == JELLI_AGENT_TOUCH_DRAG && sideways.dy == 0);
    CHECK(at(&touch, false, 0, 0, 130u).kind == JELLI_AGENT_TOUCH_NONE);
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
}

int main(void)
{
    test_a_short_press_in_place_is_a_tap_where_it_began();
    test_a_long_press_is_no_tap();
    test_moving_beyond_the_slop_drags_and_never_taps();
    test_extreme_coordinates_are_safe();
    printf("Touch checks passed\n");
    return 0;
}

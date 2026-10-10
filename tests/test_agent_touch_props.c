#include "jelli/agent_touch.h"
#include <stdio.h>
#include <stdlib.h>

/* Touch samples (ADR-016) under seeded runs of presses, wobbles, slides, lifts, and a
 * clock that sometimes runs backwards. Per press: nothing until it moves beyond the
 * slop; from then on every sample is a drag whose dy values add up to the vertical
 * movement since the press began, and its lift is no tap; a press that never left the
 * slop is a tap where it began, on its lift only, if it lasted at most
 * JELLI_AGENT_TAP_MAX_MS on a clock that did not go back. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: seed %u step %u: %s\n", __FILE__, __LINE__, seed, step,        \
                    #expr);                                                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned seed, step, taps, drags;
static uint32_t state;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

typedef struct {
    bool down, beyond;
    int start_x, start_y;
    long long moved; /* Sum of the drags' dy. */
    uint64_t start_ms;
} Press;

static bool outside_slop(const Press *press, int x, int y)
{
    int dx = x - press->start_x, dy = y - press->start_y;
    return dx < -JELLI_AGENT_TAP_SLOP || dx > JELLI_AGENT_TAP_SLOP || dy < -JELLI_AGENT_TAP_SLOP ||
           dy > JELLI_AGENT_TAP_SLOP;
}

static void lifted(Press *press, JelliAgentTouchEvent event, uint64_t now)
{
    bool tap = press->down && !press->beyond && now >= press->start_ms &&
               now - press->start_ms <= JELLI_AGENT_TAP_MAX_MS;
    CHECK(event.kind == (tap ? JELLI_AGENT_TOUCH_TAP : JELLI_AGENT_TOUCH_NONE));
    if (tap) {
        CHECK(event.x == press->start_x && event.y == press->start_y);
        ++taps;
    }
    press->down = false;
}

static void pressed(Press *press, JelliAgentTouchEvent event, int x, int y, uint64_t now)
{
    if (!press->down) { /* A new press: nothing yet. */
        *press = (Press){true, false, x, y, 0, now};
        CHECK(event.kind == JELLI_AGENT_TOUCH_NONE);
        return;
    }
    press->beyond = press->beyond || outside_slop(press, x, y);
    CHECK(event.kind == (press->beyond ? JELLI_AGENT_TOUCH_DRAG : JELLI_AGENT_TOUCH_NONE));
    if (event.kind == JELLI_AGENT_TOUCH_DRAG) {
        press->moved += event.dy;
        CHECK(press->moved == (long long)y - press->start_y); /* The drags add up. */
        ++drags;
    }
}

/* Mostly small wobbles inside the slop, sometimes slides, and now and then a lift. */
static void run(void)
{
    JelliAgentTouch touch;
    Press press = {false, false, 0, 0, 0, 0u};
    int x = 233, y = 233;
    uint64_t now = 5000u;
    state = 0x3c6ef372u ^ (seed * 2654435761u) ^ 1u;
    jelli_agent_touch_init(&touch);
    for (step = 0u; step < 2000u; ++step) {
        unsigned roll = next() % 16u;
        bool down = roll != 0u && (roll != 1u || !press.down);
        if (!press.down && down) { /* A new place to press. */
            x = (int)(next() % 1000u) - 250;
            y = (int)(next() % 1000u) - 250;
        } else if (roll < 12u) {
            x += (int)(next() % 9u) - 4;
            y += (int)(next() % 9u) - 4;
        } else {
            y += (int)(next() % 121u) - 60;
        }
        now = next() % 64u == 0u ? now - next() % 200u : now + next() % 250u;
        JelliAgentTouchEvent event = jelli_agent_touch_sample(&touch, down, x, y, now);
        if (down)
            pressed(&press, event, x, y, now);
        else
            lifted(&press, event, now);
    }
}

int main(void)
{
    for (seed = 0u; seed < 500u; ++seed)
        run();
    step = 0u;
    CHECK(taps > 1000u && drags > 10000u);
    printf("Touch properties passed (500 seeds; %u taps, %u drag samples)\n", taps, drags);
    return 0;
}

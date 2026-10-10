#include "jelli/agent_prance.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static const JelliPranceRoutine routines[] = {JELLI_PRANCE_WANDER, JELLI_PRANCE_CELEBRATE};

/* Every pose, every millisecond: a known frame, a frame that stays inside the round
 * panel (its corners may leave, but not its middle band where the unicorn is), and
 * no jump of more than a few pixels between consecutive milliseconds. */
static void every_moment_is_on_screen_and_continuous(void)
{
    for (unsigned r = 0u; r < 2u; ++r) {
        uint32_t length = jelli_agent_prance_length(routines[r]);
        CHECK(length > 3000u && length < 20000u);
        JelliPrancePose last;
        CHECK(jelli_agent_prance_at(routines[r], 0u, &last));
        CHECK(last.x == 0 && !last.mirror); /* Starts at home, facing right. */
        for (uint32_t ms = 1u; ms < length; ++ms) {
            JelliPrancePose pose;
            CHECK(jelli_agent_prance_at(routines[r], ms, &pose));
            CHECK((unsigned)pose.frame < JELLI_PRANCE_FRAMES);
            /* The frame's middle 70 % (the unicorn's body) stays inside the panel. */
            float half = (float)JELLI_AGENT_PRANCE_FRAME_PX * 0.35f;
            float dx = fabsf((float)pose.x) + half, dy = fabsf((float)pose.y) + half;
            CHECK(dx * dx + dy * dy < 233.0f * 233.0f);
            CHECK(abs(pose.x - last.x) <= 2);
            CHECK(abs(pose.y - last.y) <= 4);
            last = pose;
        }
        JelliPrancePose end;
        CHECK(!jelli_agent_prance_at(routines[r], length, &end));
        CHECK(!jelli_agent_prance_at(routines[r], UINT32_MAX, &end));
        CHECK(end.x == 0 && end.frame == JELLI_PRANCE_STAND && !end.mirror); /* Ends at home. */
    }
}

/* The wander goes right first, turns before coming back, and leaps on the way. */
static void the_wander_goes_out_turns_and_leaps(void)
{
    uint32_t length = jelli_agent_prance_length(JELLI_PRANCE_WANDER);
    int rightmost = 0, leftmost = 0, highest = 1000;
    bool turned_right_to_left = false, leapt = false, trotted = false;
    JelliPrancePose last;
    (void)jelli_agent_prance_at(JELLI_PRANCE_WANDER, 0u, &last);
    for (uint32_t ms = 0u; ms < length; ms += 5u) {
        JelliPrancePose pose;
        (void)jelli_agent_prance_at(JELLI_PRANCE_WANDER, ms, &pose);
        rightmost = pose.x > rightmost ? pose.x : rightmost;
        leftmost = pose.x < leftmost ? pose.x : leftmost;
        highest = pose.y < highest ? pose.y : highest;
        if (pose.mirror && !last.mirror)
            turned_right_to_left = pose.frame == JELLI_PRANCE_STAND && pose.x == rightmost;
        /* Facing the way it moves: right unmirrored, left mirrored. */
        if (pose.x > last.x)
            CHECK(!pose.mirror);
        if (pose.x < last.x)
            CHECK(pose.mirror);
        leapt |= pose.frame == JELLI_PRANCE_LEAP;
        trotted |= pose.frame == JELLI_PRANCE_B;
        last = pose;
    }
    CHECK(rightmost >= 80 && leftmost <= -80);
    CHECK(turned_right_to_left && leapt && trotted);
    CHECK(highest < 0); /* The leap rises above the centre line. */
}

static void celebrating_rears_and_hops(void)
{
    bool reared = false, hopped = false;
    for (uint32_t ms = 0u; ms < jelli_agent_prance_length(JELLI_PRANCE_CELEBRATE); ms += 10u) {
        JelliPrancePose pose;
        (void)jelli_agent_prance_at(JELLI_PRANCE_CELEBRATE, ms, &pose);
        reared |= pose.frame == JELLI_PRANCE_REAR;
        hopped |= pose.frame == JELLI_PRANCE_LEAP && pose.x == 0;
    }
    CHECK(reared && hopped);
}

int main(void)
{
    every_moment_is_on_screen_and_continuous();
    the_wander_goes_out_turns_and_leaps();
    celebrating_rears_and_hops();
    puts("Agent prance checks passed");
    return 0;
}

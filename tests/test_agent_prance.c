#include "jelli/agent_prance.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static const JelliPranceRoutine routines[] = {JELLI_PRANCE_WANDER, JELLI_PRANCE_CELEBRATE,
                                              JELLI_PRANCE_FROLIC};

/* Every pose, every millisecond: a known frame, a frame that stays inside the round
 * panel (its corners may leave, but not its middle band where the unicorn is), and
 * no jump of more than a few pixels between consecutive milliseconds. */
static void every_moment_is_on_screen_and_continuous(void)
{
    for (unsigned r = 0u; r < sizeof(routines) / sizeof(routines[0]); ++r) {
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

/* Petted, it leaps both ways across the screen, reaches both sides, and rears. */
static void frolicking_leaps_both_ways_and_rears(void)
{
    uint32_t length = jelli_agent_prance_length(JELLI_PRANCE_FROLIC);
    int rightmost = 0, leftmost = 0;
    unsigned leaps = 0u, rears = 0u;
    bool leapt_left = false, leapt_right = false;
    JelliPrancePose last = {JELLI_PRANCE_FRAMES, 0, 0, false}; /* So the first pose counts. */
    for (uint32_t ms = 0u; ms < length; ++ms) {
        JelliPrancePose pose;
        (void)jelli_agent_prance_at(JELLI_PRANCE_FROLIC, ms, &pose);
        rightmost = pose.x > rightmost ? pose.x : rightmost;
        leftmost = pose.x < leftmost ? pose.x : leftmost;
        leaps += pose.frame == JELLI_PRANCE_LEAP && last.frame != JELLI_PRANCE_LEAP;
        rears += pose.frame == JELLI_PRANCE_REAR && last.frame != JELLI_PRANCE_REAR;
        if (pose.frame == JELLI_PRANCE_LEAP) {
            leapt_left |= pose.x < last.x;
            leapt_right |= pose.x > last.x;
        }
        last = pose;
    }
    CHECK(rightmost >= 90 && leftmost <= -90);
    CHECK(leaps >= 4u && rears >= 3u && leapt_left && leapt_right);
    CHECK(length < jelli_agent_prance_length(JELLI_PRANCE_WANDER)); /* Quick, and joyful. */
}

static char gait(JelliPranceFrame frame)
{
    if (frame == JELLI_PRANCE_LEAP)
        return 'L';
    return frame <= JELLI_PRANCE_PASS_B ? 'T' : 0; /* The four trot frames come first. */
}

/* Appends what begins at this moment: a rear (R), a turn (|), or a leap or trot (L, T)
 * and, once it ends, the way it went (+ right, - left). */
static void narrate(const JelliPrancePose *last, const JelliPrancePose *pose, int *from,
                    char *story, size_t *n)
{
    if (gait(last->frame) && gait(pose->frame) != gait(last->frame))
        story[(*n)++] = pose->x > *from ? '+' : '-';
    if (pose->mirror != last->mirror)
        story[(*n)++] = '|';
    if (pose->frame == JELLI_PRANCE_REAR && last->frame != JELLI_PRANCE_REAR)
        story[(*n)++] = 'R';
    if (gait(pose->frame) && gait(pose->frame) != gait(last->frame)) {
        story[(*n)++] = gait(pose->frame);
        *from = last->x;
    }
}

/* Rears stand still; a leap rises well clear of the ground and lands on a landing
 * frame. peak is the highest point of the leap so far, or the ground. */
static void the_body_moves_well(const JelliPrancePose *last, const JelliPrancePose *pose,
                                int ground, int *peak)
{
    if (pose->frame == JELLI_PRANCE_REAR && last->frame == JELLI_PRANCE_REAR)
        CHECK(pose->x == last->x && pose->y == ground); /* Rearing on the spot. */
    if (last->frame == JELLI_PRANCE_LEAP && pose->frame != JELLI_PRANCE_LEAP) {
        CHECK(pose->frame == JELLI_PRANCE_LAND && pose->y == ground);
        CHECK(*peak <= ground - 50); /* A real leap, not a shuffle. */
    }
    *peak = pose->frame == JELLI_PRANCE_LEAP ? (pose->y < *peak ? pose->y : *peak) : ground;
}

/* The frolic in ADR-017's order, 5.45 s long: rears, leaps right twice, turns, leaps
 * back across twice, trots to the far side and rears there, turns, leaps and trots
 * home, and rears again; its turns are at either edge. */
static void the_frolic_tells_its_story(void)
{
    uint32_t length = jelli_agent_prance_length(JELLI_PRANCE_FROLIC);
    CHECK(length == 5450u);
    char story[64] = {0};
    size_t n = 0u;
    int from = 0, ground = 0, rears[4] = {0}, turns[4] = {0}, peak = 0;
    unsigned reared = 0u, turned = 0u;
    JelliPrancePose last = {JELLI_PRANCE_STAND, 0, 0, false}, pose;
    (void)jelli_agent_prance_at(JELLI_PRANCE_FROLIC, 0u, &pose);
    ground = pose.y;
    for (uint32_t ms = 0u; ms <= length && n < sizeof(story) - 2u; ++ms, last = pose) {
        (void)jelli_agent_prance_at(JELLI_PRANCE_FROLIC, ms, &pose);
        narrate(&last, &pose, &from, story, &n);
        the_body_moves_well(&last, &pose, ground, &peak);
        if (pose.mirror != last.mirror && turned < 4u)
            turns[turned++] = pose.x;
        if (pose.frame == JELLI_PRANCE_REAR && last.frame != JELLI_PRANCE_REAR && reared < 4u)
            rears[reared++] = pose.x;
    }
    if (strcmp(story, "RL+L+|L-L-T-R|L+T+R") != 0) {
        fprintf(stderr, "frolic story: %s\n", story);
        CHECK(false);
    }
    CHECK(reared == 3u && rears[0] == 0 && rears[1] <= -90 && rears[2] == 0);
    CHECK(turned == 2u && turns[0] >= 90 && turns[1] <= -90);
}

int main(void)
{
    every_moment_is_on_screen_and_continuous();
    the_wander_goes_out_turns_and_leaps();
    celebrating_rears_and_hops();
    frolicking_leaps_both_ways_and_rears();
    the_frolic_tells_its_story();
    puts("Agent prance checks passed");
    return 0;
}

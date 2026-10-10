/* Properties of the prancing routines over every millisecond (RFC-003): the routine
 * runs until its length and then holds its end, it is a pure function of time, it
 * faces the way it moves, it turns only on the spot, and the trot steps through its
 * cycle in order. Each check is oracle-free, so retuning a routine keeps it passing. */
#include "jelli/agent_prance.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define CHECK_AT(expr, routine, ms)                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (routine %u, ms %lu)\n", __FILE__, __LINE__, #expr,         \
                    (unsigned)(routine), (unsigned long)(ms));                                     \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static const JelliPranceRoutine routines[] = {JELLI_PRANCE_WANDER, JELLI_PRANCE_CELEBRATE};
enum { ROUTINES = sizeof(routines) / sizeof(routines[0]) };

static bool same(const JelliPrancePose *a, const JelliPrancePose *b)
{
    return a->frame == b->frame && a->x == b->x && a->y == b->y && a->mirror == b->mirror;
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

/* True for every ms before the length, false from it on, however far past (to
 * UINT32_MAX), and the pose then is the routine's last moment: it does not jump when
 * it ends. The boundary is a single, monotonic step from running to ended. */
static void it_runs_until_its_length_then_holds_its_end(void)
{
    for (unsigned r = 0u; r < ROUTINES; ++r) {
        uint32_t length = jelli_agent_prance_length(routines[r]);
        JelliPrancePose last, end, after;
        CHECK(jelli_agent_prance_at(routines[r], length - 1u, &last));
        CHECK(!jelli_agent_prance_at(routines[r], length, &end));
        CHECK(same(&last, &end));
        CHECK(end.frame == JELLI_PRANCE_STAND && end.x == 0 && !end.mirror);
        const uint32_t past[] = {length + 1u, length + 1000u,  0x7FFFFFFFu,
                                 0x80000000u, UINT32_MAX - 1u, UINT32_MAX};
        for (size_t i = 0u; i < sizeof(past) / sizeof(past[0]); ++i) {
            CHECK_AT(!jelli_agent_prance_at(routines[r], past[i], &after), routines[r], past[i]);
            CHECK_AT(same(&after, &end), routines[r], past[i]);
        }
        uint32_t seed = 0x2545F491u + r;
        for (unsigned i = 0u; i < 20000u; ++i) {
            uint32_t ms = next(&seed);
            bool running = jelli_agent_prance_at(routines[r], ms, &after);
            CHECK_AT(running == (ms < length), routines[r], ms);
            if (!running)
                CHECK_AT(same(&after, &end), routines[r], ms);
        }
    }
}

/* The same moment gives the same pose whatever was asked before: forwards, backwards,
 * at random, and interleaved with the other routine. No hidden state. */
static void it_is_a_pure_function_of_time(void)
{
    enum { MAX_LENGTH = 20000 };
    static JelliPrancePose forward[MAX_LENGTH];
    for (unsigned r = 0u; r < ROUTINES; ++r) {
        uint32_t length = jelli_agent_prance_length(routines[r]);
        CHECK(length > 0u && length <= MAX_LENGTH);
        for (uint32_t ms = 0u; ms < length; ++ms)
            CHECK(jelli_agent_prance_at(routines[r], ms, &forward[ms]));
        JelliPrancePose pose, other;
        for (uint32_t ms = length; ms-- > 0u;) {
            (void)jelli_agent_prance_at(routines[ROUTINES - 1u - r], length - 1u - ms, &other);
            CHECK(jelli_agent_prance_at(routines[r], ms, &pose));
            CHECK_AT(same(&pose, &forward[ms]), routines[r], ms);
        }
        uint32_t seed = 0x9E3779B9u ^ r;
        for (unsigned i = 0u; i < 5000u; ++i) {
            uint32_t ms = next(&seed) % length;
            CHECK(jelli_agent_prance_at(routines[r], ms, &pose));
            CHECK_AT(same(&pose, &forward[ms]), routines[r], ms);
        }
    }
}

/* Moving right it faces right (unmirrored), moving left it faces left; it turns only
 * where it stands, and is shown standing, facing its new way, as it turns. */
static void it_faces_the_way_it_moves_and_turns_on_the_spot(void)
{
    for (unsigned r = 0u; r < ROUTINES; ++r) {
        uint32_t length = jelli_agent_prance_length(routines[r]);
        JelliPrancePose last, pose;
        (void)jelli_agent_prance_at(routines[r], 0u, &last);
        unsigned turns = 0u;
        for (uint32_t ms = 1u; ms <= length; ++ms) {
            (void)jelli_agent_prance_at(routines[r], ms, &pose);
            if (pose.mirror != last.mirror) {
                /* A trot interpolates up to, not onto, its end, so the hold after it may
                 * arrive by one pixel on the very millisecond it turns. */
                ++turns;
                CHECK_AT(abs(pose.x - last.x) <= 1, routines[r], ms);
                CHECK_AT(pose.frame == JELLI_PRANCE_STAND, routines[r], ms);
            } else if (pose.x != last.x) {
                CHECK_AT(pose.mirror == (pose.x < last.x), routines[r], ms);
            }
            last = pose;
        }
        /* Every turn away is matched by a turn back: it ends facing right as it began. */
        CHECK(turns % 2u == 0u && turns > 0u);
    }
}

static bool trotting(JelliPranceFrame frame)
{
    return frame == JELLI_PRANCE_A || frame == JELLI_PRANCE_PASS_A || frame == JELLI_PRANCE_B ||
           frame == JELLI_PRANCE_PASS_B;
}

static JelliPranceFrame after_in_cycle(JelliPranceFrame frame)
{
    switch (frame) {
    case JELLI_PRANCE_A:
        return JELLI_PRANCE_PASS_A;
    case JELLI_PRANCE_PASS_A:
        return JELLI_PRANCE_B;
    case JELLI_PRANCE_B:
        return JELLI_PRANCE_PASS_B;
    case JELLI_PRANCE_PASS_B:
    case JELLI_PRANCE_LEAP:
    case JELLI_PRANCE_LAND:
    case JELLI_PRANCE_REAR:
    case JELLI_PRANCE_STAND:
    case JELLI_PRANCE_FRAMES:
    default:
        return JELLI_PRANCE_A;
    }
}

/* Within a trot the frames go A, passing, B, passing, A ... with no frame held longer
 * than one step, passing frames sit a few pixels lower than the planted ones, and
 * every grounded frame shares one ground line that a leap only ever rises above. */
static void the_trot_steps_through_its_cycle_on_one_ground(void)
{
    for (unsigned r = 0u; r < ROUTINES; ++r) {
        uint32_t length = jelli_agent_prance_length(routines[r]);
        JelliPrancePose last, pose;
        (void)jelli_agent_prance_at(routines[r], 0u, &last);
        int ground = last.y, passing = 0;
        uint32_t held = 1u;
        for (uint32_t ms = 1u; ms < length; ++ms) {
            CHECK(jelli_agent_prance_at(routines[r], ms, &pose));
            held = pose.frame == last.frame ? held + 1u : 1u;
            if (trotting(pose.frame) && trotting(last.frame) && pose.frame != last.frame)
                CHECK_AT(pose.frame == after_in_cycle(last.frame), routines[r], ms);
            if (trotting(pose.frame))
                CHECK_AT(held <= 140u, routines[r], ms);
            if (pose.frame == JELLI_PRANCE_PASS_A || pose.frame == JELLI_PRANCE_PASS_B) {
                passing = passing ? passing : pose.y;
                CHECK_AT(pose.y == passing, routines[r], ms);
            } else if (pose.frame == JELLI_PRANCE_LEAP) {
                CHECK_AT(pose.y <= ground && pose.y >= ground - JELLI_AGENT_PRANCE_FRAME_PX / 4,
                         routines[r], ms);
            } else {
                CHECK_AT(pose.y == ground, routines[r], ms);
            }
            last = pose;
        }
        CHECK(passing > ground && passing <= ground + 6);
    }
}

/* A leap rises smoothly to one peak near its middle and comes down where it lands:
 * height never falls then rises again within one leap. */
static void a_leap_has_one_peak(void)
{
    for (unsigned r = 0u; r < ROUTINES; ++r) {
        uint32_t length = jelli_agent_prance_length(routines[r]);
        JelliPrancePose pose;
        bool in_leap = false, falling = false;
        int last_y = 0, leaps = 0;
        for (uint32_t ms = 0u; ms < length; ++ms) {
            (void)jelli_agent_prance_at(routines[r], ms, &pose);
            bool leaping = pose.frame == JELLI_PRANCE_LEAP;
            if (leaping && !in_leap) {
                ++leaps;
                falling = false;
            } else if (leaping) {
                falling = falling || pose.y > last_y;
                if (falling)
                    CHECK_AT(pose.y >= last_y, routines[r], ms);
            }
            in_leap = leaping;
            last_y = pose.y;
        }
        CHECK(leaps >= 1);
    }
}

int main(void)
{
    it_runs_until_its_length_then_holds_its_end();
    it_is_a_pure_function_of_time();
    it_faces_the_way_it_moves_and_turns_on_the_spot();
    the_trot_steps_through_its_cycle_on_one_ground();
    a_leap_has_one_peak();
    puts("Agent prance property checks passed");
    return 0;
}

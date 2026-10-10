#include "jelli/agent_prance.h"
#include <math.h>
#include <stddef.h>

enum {
    GROUND = 30,   /* The frame's centre while hooves are on the ground, below centre. */
    REACH = 90,    /* How far either side it trots. */
    STEP_MS = 140, /* Each trot frame. */
    BOUNCE = 3,    /* Passing frames sit this much lower. */
    HOP = 60       /* Height of a leap. */
};

typedef enum { HOLD, TROT, LEAP } Kind;

/* One piece of a routine: a held frame, a trot from x0 to x1, or a leap from x0 to x1. */
typedef struct {
    Kind kind;
    JelliPranceFrame frame;
    int x0, x1;
    uint16_t ms;
    bool mirror;
} Step;

#define HOLD_AT(frame, x, ms, mirror) {HOLD, frame, x, x, ms, mirror}
#define TROT_TO(x0, x1, ms, mirror) {TROT, JELLI_PRANCE_A, x0, x1, ms, mirror}
#define LEAP_TO(x0, x1, ms, mirror) {LEAP, JELLI_PRANCE_LEAP, x0, x1, ms, mirror}

/* Out to the right, turn, back across with a leap, turn, home to the centre. */
static const Step wander[] = {
    HOLD_AT(JELLI_PRANCE_STAND, 0, 600, false),
    TROT_TO(0, REACH, 1000, false),
    HOLD_AT(JELLI_PRANCE_STAND, REACH, 300, false),
    HOLD_AT(JELLI_PRANCE_STAND, REACH, 300, true),
    TROT_TO(REACH, 30, 700, true),
    LEAP_TO(30, -40, 500, true),
    HOLD_AT(JELLI_PRANCE_LAND, -40, 200, true),
    TROT_TO(-40, -REACH, 600, true),
    HOLD_AT(JELLI_PRANCE_STAND, -REACH, 300, true),
    HOLD_AT(JELLI_PRANCE_STAND, -REACH, 300, false),
    TROT_TO(-REACH, 0, 1000, false),
    HOLD_AT(JELLI_PRANCE_STAND, 0, 600, false),
};

/* Rears, hops on the spot, rears again, a little victory trot. */
static const Step celebrate[] = {
    HOLD_AT(JELLI_PRANCE_REAR, 0, 700, false),
    HOLD_AT(JELLI_PRANCE_STAND, 0, 150, false),
    LEAP_TO(0, 0, 500, false),
    HOLD_AT(JELLI_PRANCE_LAND, 0, 200, false),
    HOLD_AT(JELLI_PRANCE_REAR, 0, 700, false),
    TROT_TO(0, 60, 500, false),
    HOLD_AT(JELLI_PRANCE_STAND, 60, 200, true),
    TROT_TO(60, 0, 500, true),
    HOLD_AT(JELLI_PRANCE_STAND, 0, 400, false),
};

static const Step *steps(JelliPranceRoutine routine, size_t *count)
{
    if (routine == JELLI_PRANCE_CELEBRATE) {
        *count = sizeof(celebrate) / sizeof(celebrate[0]);
        return celebrate;
    }
    *count = sizeof(wander) / sizeof(wander[0]);
    return wander;
}

uint32_t jelli_agent_prance_length(JelliPranceRoutine routine)
{
    size_t count = 0u;
    const Step *list = steps(routine, &count);
    uint32_t total = 0u;
    for (size_t i = 0u; i < count; ++i)
        total += list[i].ms;
    return total;
}

static JelliPrancePose pose_in(const Step *step, uint32_t into)
{
    JelliPrancePose pose = {step->frame, step->x0, GROUND, step->mirror};
    int travelled = (step->x1 - step->x0) * (int)into / (int)step->ms;
    pose.x = step->x0 + travelled;
    if (step->kind == TROT) {
        static const JelliPranceFrame cycle[] = {JELLI_PRANCE_A, JELLI_PRANCE_PASS_A,
                                                 JELLI_PRANCE_B, JELLI_PRANCE_PASS_B};
        pose.frame = cycle[into / STEP_MS % 4u];
        pose.y +=
            pose.frame == JELLI_PRANCE_PASS_A || pose.frame == JELLI_PRANCE_PASS_B ? BOUNCE : 0;
    } else if (step->kind == LEAP) {
        float p = (float)into / (float)step->ms;
        pose.y -= (int)lrintf((float)HOP * sinf(3.14159265f * p));
    }
    return pose;
}

bool jelli_agent_prance_at(JelliPranceRoutine routine, uint32_t ms, JelliPrancePose *pose)
{
    size_t count = 0u;
    const Step *list = steps(routine, &count);
    for (size_t i = 0u; i < count; ++i) {
        if (ms < list[i].ms) {
            *pose = pose_in(&list[i], ms);
            return true;
        }
        ms -= list[i].ms;
    }
    const Step *last = &list[count - 1u];
    *pose = (JelliPrancePose){last->frame, last->x1, GROUND, last->mirror};
    return false;
}

#ifndef JELLI_AGENT_TOUCH_H
#define JELLI_AGENT_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

/* Loki Agent's touches (ADR-016, ADR-017): the host samples the touch controller now
 * and then (pressed or not, and where) and this turns the samples into gestures:
 *
 * - A press that stays within JELLI_AGENT_TAP_SLOP pixels of where it began is a tap
 *   at that point when it lifts within JELLI_AGENT_TAP_MAX_MS.
 * - Where holds are allowed, such a press that a sample sees still in place at
 *   JELLI_AGENT_HOLD_MS or later is a hold instead: HOLD then, and RELEASE when it
 *   lifts, however far it moved while held. A hold is never a tap. (A press lifted
 *   before any sample saw it that late is still a tap; the board samples every 40 ms.)
 * - A press that moves further is a drag from then on, reported as the vertical
 *   movement since the last sample, and when it lifts a STROKE carrying the distance
 *   the finger travelled from where it began, counted once it left the slop. A drag is
 *   never a tap or a hold.
 *
 * Fixed state; times are the host's monotonic milliseconds. */

enum { JELLI_AGENT_TAP_SLOP = 14, JELLI_AGENT_TAP_MAX_MS = 800, JELLI_AGENT_HOLD_MS = 400 };

typedef enum {
    JELLI_AGENT_TOUCH_NONE,
    JELLI_AGENT_TOUCH_TAP,
    JELLI_AGENT_TOUCH_DRAG,
    JELLI_AGENT_TOUCH_HOLD,
    JELLI_AGENT_TOUCH_RELEASE,
    JELLI_AGENT_TOUCH_STROKE
} JelliAgentTouchKind;

typedef struct {
    JelliAgentTouchKind kind;
    int x, y;      /* TAP, HOLD, STROKE: where the press began. */
    int dy;        /* DRAG: vertical movement since the last sample, down positive. */
    uint32_t path; /* STROKE: pixels travelled (across plus down, each step), capped. */
} JelliAgentTouchEvent;

typedef struct {
    int start_x, start_y, last_x, last_y;
    uint64_t start_ms;
    uint32_t path;
    bool down, dragging, holding, holds;
} JelliAgentTouch;

/* Holds off: a still press is only ever a tap or nothing. */
void jelli_agent_touch_init(JelliAgentTouch *touch);

/* Whether a still press may become a hold (the unicorn's screen, not Settings). A hold
 * already under way still ends with RELEASE. */
void jelli_agent_touch_holds(JelliAgentTouch *touch, bool allowed);

/* One sample: pressed at (x, y), or not pressed (x and y ignored). */
JelliAgentTouchEvent jelli_agent_touch_sample(JelliAgentTouch *touch, bool pressed, int x, int y,
                                              uint64_t now_ms);

#endif

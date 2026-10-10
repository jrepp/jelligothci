#ifndef JELLI_AGENT_TOUCH_H
#define JELLI_AGENT_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

/* Loki Agent's touches (ADR-016): the host samples the touch controller now and then
 * (pressed or not, and where) and this turns the samples into taps and vertical drags.
 * A press that stays within JELLI_AGENT_TAP_SLOP pixels of where it began is a tap at
 * that point when it lifts; one that moves further is a drag from then on, reported
 * as the vertical movement since the last sample, and never becomes a tap. Fixed
 * state; times are the host's monotonic milliseconds. */

enum { JELLI_AGENT_TAP_SLOP = 14, JELLI_AGENT_TAP_MAX_MS = 800 };

typedef enum {
    JELLI_AGENT_TOUCH_NONE,
    JELLI_AGENT_TOUCH_TAP,
    JELLI_AGENT_TOUCH_DRAG
} JelliAgentTouchKind;

typedef struct {
    JelliAgentTouchKind kind;
    int x, y; /* TAP: where the press began. */
    int dy;   /* DRAG: vertical movement since the last sample, down positive. */
} JelliAgentTouchEvent;

typedef struct {
    int start_x, start_y, last_y;
    uint64_t start_ms;
    bool down, dragging;
} JelliAgentTouch;

void jelli_agent_touch_init(JelliAgentTouch *touch);

/* One sample: pressed at (x, y), or not pressed (x and y ignored). A press longer than
 * JELLI_AGENT_TAP_MAX_MS without moving is not a tap either: it is held, not tapped. */
JelliAgentTouchEvent jelli_agent_touch_sample(JelliAgentTouch *touch, bool pressed, int x, int y,
                                              uint64_t now_ms);

#endif

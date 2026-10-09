#ifndef JELLI_AGENT_PRESENT_H
#define JELLI_AGENT_PRESENT_H

#include "jelli/engine.h"
#include <stdbool.h>
#include <stddef.h>

/* How the agent sends a damage rectangle to a panel whose windows need even starts and
 * even exclusive ends (the CO5300), through a DMA stripe that holds a fixed number of
 * pixels (RFC-003 render spike). Pure arithmetic: no allocation, no platform state. */
typedef struct {
    unsigned x0, y0, x1, y1; /* Even starts and even exclusive ends, inside the surface. */
    unsigned rows_per_send;  /* Even, at least 2; rows_per_send * (x1 - x0) fits the stripe. */
} JelliAgentWindow;

/* The smallest even-edged window covering rect clipped to a width x height surface
 * (sums are 64-bit, so huge rectangle fields clip rather than wrap), and the most
 * rows of it one stripe of stripe_pixels holds. False when there is nothing to send:
 * the clipped rect is empty, a surface size is zero or odd, or the stripe cannot hold
 * two rows of the window. window is written only on success. */
bool jelli_agent_present_window(JelliRect rect, unsigned width, unsigned height,
                                size_t stripe_pixels, JelliAgentWindow *window);

/* Rows in the send that starts at row y of window: rows_per_send, or what remains
 * before y1 (zero once y reaches y1). Sends start at y0 and advance by rows_per_send. */
unsigned jelli_agent_window_rows(const JelliAgentWindow *window, unsigned y);

#endif

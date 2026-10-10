#ifndef JELLI_AGENT_SCREEN_H
#define JELLI_AGENT_SCREEN_H

#include "jelli/engine.h"
#include <stdbool.h>

/* Brings up the panel without LVGL and draws the identity screen (a lilac ring) until
 * the UI's first frame, so the running app is visible at a glance. Returns false when
 * the display cannot be driven; the console keeps working. */
bool jelli_agent_screen_start(void);

/* Copies rect of a native-endian RGB565 466x466 surface to the panel through the DMA
 * stripe, widened to even window edges, and waits for each transfer. Any task, one
 * at a time. False once a transfer has timed out: the stripe is then never reused. */
bool jelli_agent_screen_present(const JelliSurface *surface, JelliRect rect);

/* The panel's light on (the usual brightness) or off, for screen sleep (ADR-016):
 * present a black frame first, so nothing shows even at a faint level. Any task. */
bool jelli_agent_screen_light(bool on);

#endif

#ifndef JELLI_AGENT_SCREEN_H
#define JELLI_AGENT_SCREEN_H

#include <stdbool.h>

/* Brings up the panel without LVGL and draws the phase 0 identity screen (a lilac
 * ring) so the running app is visible at a glance. Returns false when the display
 * cannot be driven; the console keeps working. */
bool jelli_agent_screen_start(void);

#endif

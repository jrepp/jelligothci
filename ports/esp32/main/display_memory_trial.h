#ifndef JELLI_DISPLAY_MEMORY_TRIAL_H
#define JELLI_DISPLAY_MEMORY_TRIAL_H
#include "display_output.h"
/* Power-only; benchmark borrows canvas and drained DMA staging under LVGL lock. */
void jelli_display_memory_trial(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id,
                                bool benchmark);
#endif

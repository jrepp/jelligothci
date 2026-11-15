#ifndef JELLI_AUTO_SLEEP_H
#define JELLI_AUTO_SLEEP_H
#include "display_output.h"
#include "esp_pm.h"
#include <stdbool.h>

typedef struct {
    esp_pm_config_t previous;
    bool active, paused, audio_closed, panel_slept, gpio_enabled, wake_enabled;
} JelliAutoSleep;

/* Engine task only. Call with the screen blank and networking idle. */
bool jelli_auto_sleep_enter(JelliAutoSleep *sleep, JelliDisplayOutput *display);
void jelli_auto_sleep_leave(JelliAutoSleep *sleep, JelliDisplayOutput *display);
#endif

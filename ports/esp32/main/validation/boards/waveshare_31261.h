#ifndef JELLI_WAVESHARE_31261_PROBE_H
#define JELLI_WAVESHARE_31261_PROBE_H
#include "jelli/debug.h"
#include "esp_lcd_touch.h"
#include "touch_input.h"
#include <stdint.h>

void jelli_power_probe_init(JelliTouchGuard *guard);
uint32_t jelli_power_probe_irqs(void);
esp_err_t jelli_power_probe_reset(unsigned delay_ms);
void jelli_power_probe_report(JelliDebug *debug, uint32_t id, int brightness);
#endif

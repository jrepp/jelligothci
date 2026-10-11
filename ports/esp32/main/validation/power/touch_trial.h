#ifndef JELLI_TOUCH_TRIAL_H
#define JELLI_TOUCH_TRIAL_H
#include "jelli/debug.h"
#include "esp_lv_adapter_input.h"

/* All methods except command entry run under the BSP display lock. */
bool jelli_touch_trial_command(JelliDebug *debug, uint32_t id, char **words, unsigned count);
void jelli_touch_trial_poll(void);
bool jelli_touch_trial_busy(void);
bool jelli_touch_trial_event(lv_event_code_t code);
esp_err_t jelli_touch_trial_read(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points,
                                 uint8_t *count, uint8_t capacity, void *ctx);
#endif

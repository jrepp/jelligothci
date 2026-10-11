#ifndef JELLI_TOUCH_INPUT_H
#define JELLI_TOUCH_INPUT_H
#include "jelli/touch_guard.h"
#include "esp_lv_adapter_input.h"

esp_err_t jelli_touch_input_read(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points,
                                 uint8_t *count, uint8_t capacity, void *ctx);
esp_err_t jelli_touch_input_init(JelliTouchGuard *guard, JelliGesture *gesture, lv_indev_t *indev);
#endif

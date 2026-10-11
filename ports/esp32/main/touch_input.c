#include "touch_input.h"

esp_err_t jelli_touch_input_read(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points,
                                 uint8_t *count, uint8_t capacity, void *ctx)
{
    JelliTouchGuard *guard = ctx;
    *count = 0;
    esp_err_t result = esp_lcd_touch_read_data(tp);
    if (result == ESP_OK)
        result = esp_lcd_touch_get_data(tp, points, count, capacity);
    /* An adapter error becomes LVGL RELEASED. Cancel before that event so it
     * cannot commit a tap; ignore continued contact until a valid release. */
    jelli_touch_guard_apply(guard, result != ESP_OK, count);
    return result;
}

esp_err_t jelli_touch_input_init(JelliTouchGuard *guard, JelliGesture *gesture, lv_indev_t *indev)
{
    *guard = (JelliTouchGuard){.gesture = gesture};
    const esp_lv_adapter_touch_callbacks_t callbacks = {.custom_touch_read = jelli_touch_input_read,
                                                        .user_ctx = guard};
    return esp_lv_adapter_set_touch_callbacks(indev, &callbacks);
}

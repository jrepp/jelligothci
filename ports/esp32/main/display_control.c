#include "display_output.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_lcd_co5300.h"

/* Caller holds the display lock. */
JelliDeviceResult jelli_display_brightness_locked(JelliDisplayOutput *output, unsigned percent)
{
    if (percent > 100u)
        return JELLI_DEVICE_INVALID;
    if (!output->transfer.panel)
        return JELLI_DEVICE_UNAVAILABLE;
    esp_err_t result =
        esp_lcd_panel_co5300_set_brightness(output->transfer.panel, (uint8_t)percent);
    output->brightness_known = result == ESP_OK;
    if (result == ESP_OK)
        output->brightness_percent = percent;
    return result == ESP_FAIL ? JELLI_DEVICE_IO : result;
}

static JelliDeviceResult brightness(void *ctx, unsigned percent)
{
    esp_err_t locked = bsp_display_lock(100);
    if (locked != ESP_OK)
        return locked == ESP_FAIL ? JELLI_DEVICE_IO : locked;
    JelliDeviceResult result = jelli_display_brightness_locked(ctx, percent);
    bsp_display_unlock();
    return result;
}

JelliDisplayDriver jelli_display_output_driver(JelliDisplayOutput *output)
{
    return (JelliDisplayDriver){output, brightness};
}

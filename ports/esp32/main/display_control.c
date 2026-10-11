#include "display_output.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_lcd_co5300.h"

static JelliDeviceResult brightness(void *ctx, unsigned percent)
{
    JelliDisplayOutput *output = ctx;
    if (percent > 100u)
        return JELLI_DEVICE_INVALID;
    if (!output->transfer.panel)
        return JELLI_DEVICE_UNAVAILABLE;
    esp_err_t locked = bsp_display_lock(100);
    if (locked != ESP_OK)
        return locked == ESP_FAIL ? JELLI_DEVICE_IO : locked;
    /* The BSP setter discards this driver's SPI error. Keep the requested
     * percent here; a cache-derived byte->percent round trip loses precision. */
    esp_err_t result =
        esp_lcd_panel_co5300_set_brightness(output->transfer.panel, (uint8_t)percent);
    output->brightness_known = result == ESP_OK;
    if (result == ESP_OK)
        output->brightness_percent = percent;
    bsp_display_unlock();
    return result == ESP_FAIL ? JELLI_DEVICE_IO : result;
}

JelliDisplayDriver jelli_display_output_driver(JelliDisplayOutput *output)
{
    return (JelliDisplayDriver){output, brightness};
}

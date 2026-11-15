#include "auto_sleep.h"
#include "sound_output.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "esp_log.h"

static const char *TAG = "auto_sleep";

void jelli_auto_sleep_leave(JelliAutoSleep *sleep, JelliDisplayOutput *display)
{
    if (sleep->active && esp_pm_configure(&sleep->previous) != ESP_OK)
        ESP_LOGE(TAG, "Could not restore power management");
    if (sleep->wake_enabled)
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (sleep->gpio_enabled)
        (void)gpio_wakeup_disable(BSP_LCD_TOUCH_INT);
    if (sleep->panel_slept && esp_lcd_panel_disp_sleep(display->transfer.panel, false) != ESP_OK)
        ESP_LOGE(TAG, "Could not wake panel");
    if (sleep->panel_slept && display->brightness_known &&
        jelli_display_brightness(jelli_display_output_driver(display),
                                 display->brightness_percent) != JELLI_DEVICE_OK)
        ESP_LOGE(TAG, "Could not restore brightness");
    if (sleep->audio_closed && !jelli_sound_output_enable(true))
        ESP_LOGE(TAG, "Could not reopen audio");
    if (sleep->paused && esp_lv_adapter_resume() != ESP_OK)
        ESP_LOGE(TAG, "Could not resume display adapter");
    if (sleep->paused && bsp_display_lock(1000) == ESP_OK) {
        lv_obj_invalidate(display->canvas);
        bsp_display_unlock();
    }
    *sleep = (JelliAutoSleep){0};
}

bool jelli_auto_sleep_enter(JelliAutoSleep *sleep, JelliDisplayOutput *display)
{
    if (!display->transfer.panel || esp_pm_get_configuration(&sleep->previous) != ESP_OK)
        return false;
    if (esp_lv_adapter_pause(2000) != ESP_OK)
        return false;
    sleep->paused = true;
    sleep->audio_closed = jelli_sound_output_enable(false);
    if (!sleep->audio_closed)
        goto fail;
    sleep->panel_slept = true; /* The driver can enter sleep before reporting a later error. */
    if (esp_lcd_panel_disp_sleep(display->transfer.panel, true) != ESP_OK)
        goto fail;
    if (gpio_wakeup_enable(BSP_LCD_TOUCH_INT, GPIO_INTR_LOW_LEVEL) != ESP_OK)
        goto fail;
    sleep->gpio_enabled = true;
    if (esp_sleep_enable_gpio_wakeup() != ESP_OK)
        goto fail;
    sleep->wake_enabled = true;
    esp_pm_config_t config = {.max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true};
    sleep->active = true; /* Restore the previous policy even if configuration fails. */
    if (esp_pm_configure(&config) != ESP_OK)
        goto fail;
    ESP_LOGI(TAG, "Battery light sleep enabled");
    return true;
fail:
    ESP_LOGW(TAG, "Sleep setup failed; restoring display and audio");
    jelli_auto_sleep_leave(sleep, display);
    return false;
}

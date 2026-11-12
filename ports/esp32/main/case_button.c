#include "case_button.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_log.h"

/* SKU 31261: PWR's conditioned SYS_OUT is EXIO4, active high. */
static const char *TAG = "case_button";

void jelli_case_button_init(JelliCaseButton *button, JelliDisplayOutput *display,
                            JelliTouchGuard *touch)
{
    *button = (JelliCaseButton){.display = display, .touch = touch};
    esp_io_expander_handle_t expander = bsp_io_expander_init();
    const i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                        .device_address = BSP_IO_EXPANDER_I2C_ADDRESS,
                                        .scl_speed_hz = 100000};
    if (!expander ||
        esp_io_expander_set_dir(expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_INPUT) != ESP_OK ||
        i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &button->io) != ESP_OK) {
        button->io = NULL;
        ESP_LOGW(TAG, "PWR input unavailable");
    } else {
        ESP_LOGI(TAG, "PWR input ready on EXIO4");
    }
}

static void toggle(JelliCaseButton *button)
{
    if (bsp_display_lock(100) != ESP_OK) {
        ESP_LOGW(TAG, "Display lock unavailable");
        return;
    }
    unsigned target = button->blank ? button->restore_brightness : 0u;
    if (!button->blank)
        button->restore_brightness = button->display->brightness_percent;
    if (jelli_display_brightness_locked(button->display, target) != JELLI_DEVICE_OK) {
        bsp_display_unlock();
        ESP_LOGW(TAG, "Display brightness change failed");
        return;
    }
    button->blank = !button->blank;
    button->touch->gesture->active = false;
    button->touch->awaiting_release = true;
    bsp_display_unlock();
    ESP_LOGI(TAG, "Display %s by PWR", button->blank ? "blanked" : "restored");
}

void jelli_case_button_poll(JelliCaseButton *button, uint64_t now)
{
    uint8_t level = 0;
    const uint8_t input_register = 0;
    if (!button->io)
        return;
    bool valid =
        i2c_master_transmit_receive(button->io, &input_register, 1, &level, 1, 10) == ESP_OK;
    if (jelli_case_button_sample(&button->state, valid, (level & IO_EXPANDER_PIN_NUM_4) != 0, now))
        toggle(button);
}

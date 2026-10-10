#include "input.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "driver/gpio.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"

static const char *TAG = "agent_input";
static esp_lcd_touch_handle_t touch;
static bool button_ready;
#ifdef JELLI_BOARD_BOOT_KEY_GPIO
/* Consecutive samples the button was down, and whether its press was reported. */
static unsigned held;
static bool reported;
#endif

static bool start_button(void)
{
#ifdef JELLI_BOARD_BOOT_KEY_GPIO
    /* BOOT is a strapping pin, read only after start-up; pressed pulls it low. */
    gpio_config_t input = {.pin_bit_mask = 1ull << JELLI_BOARD_BOOT_KEY_GPIO,
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
    return gpio_config(&input) == ESP_OK;
#else
    return false;
#endif
}

bool jelli_agent_input_start(void)
{
    /* The orientation the pet's LVGL input uses on this panel. */
    const bsp_display_cfg_t config = {.touch_flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1}};
    if (bsp_touch_new(&config, &touch) != ESP_OK)
        touch = NULL;
    button_ready = start_button();
    ESP_LOGI(TAG, "Touch %s; side button %s", touch ? "ready" : "unavailable",
             button_ready ? "ready" : "unavailable");
    return touch != NULL;
}

bool jelli_agent_input_touch(bool *pressed, int *x, int *y)
{
    *pressed = false;
    if (!touch || esp_lcd_touch_read_data(touch) != ESP_OK)
        return false;
    esp_lcd_touch_point_data_t point;
    uint8_t count = 0u;
    if (esp_lcd_touch_get_data(touch, &point, &count, 1u) == ESP_OK && count) {
        *pressed = true;
        *x = point.x;
        *y = point.y;
    }
    return true;
}

bool jelli_agent_input_button(void)
{
#ifdef JELLI_BOARD_BOOT_KEY_GPIO
    if (!button_ready)
        return false;
    if (gpio_get_level((gpio_num_t)JELLI_BOARD_BOOT_KEY_GPIO) != 0) {
        held = 0u;
        reported = false;
        return false;
    }
    if (held < 2u)
        ++held;
    if (held < 2u || reported)
        return false;
    reported = true;
    return true;
#else
    return false;
#endif
}

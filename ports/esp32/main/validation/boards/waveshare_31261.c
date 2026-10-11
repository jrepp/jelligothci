#include "waveshare_31261.h"
#include "pmic_probe.h"
#include "../power/touch_trial.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_lv_adapter_input.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdio.h>

static portMUX_TYPE irq_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t interrupts;
static i2c_master_dev_handle_t pmic, touch_device;

static void IRAM_ATTR touch_irq(esp_lcd_touch_handle_t touch, void *ctx)
{
    (void)touch;
    (void)ctx;
    portENTER_CRITICAL_ISR(&irq_lock);
    ++interrupts;
    portEXIT_CRITICAL_ISR(&irq_lock);
}

void jelli_power_probe_init(JelliTouchGuard *guard)
{
    const esp_lv_adapter_touch_callbacks_t callbacks = {
        .on_interrupt = touch_irq, .custom_touch_read = jelli_touch_trial_read, .user_ctx = guard};
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    ESP_ERROR_CHECK(esp_lv_adapter_set_touch_callbacks(bsp_display_get_input_dev(), &callbacks));
    bsp_display_unlock();
    ESP_ERROR_CHECK(jelli_pmic_probe_open(bsp_i2c_get_handle(), &pmic));
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x5a, .scl_speed_hz = 100000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &touch_device));
}

uint32_t jelli_power_probe_irqs(void)
{
    portENTER_CRITICAL(&irq_lock);
    uint32_t count = interrupts;
    portEXIT_CRITICAL(&irq_lock);
    return count;
}

void jelli_power_probe_report(JelliDebug *debug, uint32_t id, int brightness)
{
    uint8_t values[4];
    esp_err_t result = jelli_pmic_probe_read(pmic, values);
    char body[384];
    int size = snprintf(
        body, sizeof(body),
        "{\"ok\":true,\"schema\":1,\"board\":\"waveshare-31261\",\"profile\":\"power\",\"irqs\":%"
        "lu,\"int_level\":%d,\"pa_level\":%d,"
        "\"pmic_error\":%d,\"pmic_regs_03_26_80_90\":[%u,%u,%u,%u],"
        "\"internal_free\":%u,\"brightness\":%d,\"retention_largest\":%u}",
        (unsigned long)jelli_power_probe_irqs(), gpio_get_level(BSP_LCD_TOUCH_INT),
        gpio_get_level(BSP_POWER_AMP_IO), (int)result, values[0], values[1], values[2], values[3],
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), brightness,
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_RETENTION));
    if (size > 0 && (size_t)size < sizeof(body))
        jelli_debug_response(debug, id, body);
}

static esp_err_t reset_read(unsigned delay_ms)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_LCD_TOUCH_RST, 0), "power", "reset low");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_LCD_TOUCH_RST, 1), "power", "reset high");
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    /* Reproduce the pinned driver's split writes, without retrying a failure. */
    const uint8_t command[] = {0xd1, 0x01};
    const uint8_t check[] = {0xd1, 0xfc};
    uint8_t data[4];
    ESP_RETURN_ON_ERROR(i2c_master_transmit(touch_device, command, 2, 50), "power",
                        "command address");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(i2c_master_transmit(touch_device, command, 2, 50), "power", "command data");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(i2c_master_transmit(touch_device, check, 2, 50), "power", "check address");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(i2c_master_receive(touch_device, data, 4, 50), "power", "check data");
    return data[2] == 0xca && data[3] == 0xca ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t jelli_power_probe_reset(unsigned delay_ms)
{
    unsigned failed = 0;
    for (unsigned n = 0; n < 10u; ++n) {
        esp_err_t result = reset_read(delay_ms);
        ESP_LOGI("power", "touch reset delay=%u trial=%u result=%d", delay_ms, n, (int)result);
        if (result != ESP_OK)
            ++failed;
    }
    /* Leave controller in its normal post-reset state, even after a failed read. */
    esp_err_t low = gpio_set_level(BSP_LCD_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_err_t high = gpio_set_level(BSP_LCD_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    return !failed && low == ESP_OK && high == ESP_OK ? ESP_OK : ESP_FAIL;
}

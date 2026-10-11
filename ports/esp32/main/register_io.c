#include "register_io.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static JelliDeviceResult read_register(void *ctx, uint8_t reg, uint8_t *data, size_t count)
{
    if (!ctx)
        return JELLI_DEVICE_UNAVAILABLE;
    if (!data || !count || count > 16u)
        return JELLI_DEVICE_INVALID;
    esp_err_t result = i2c_master_transmit_receive(ctx, &reg, 1, data, count, 20);
    return result == ESP_FAIL ? JELLI_DEVICE_IO : result;
}

static JelliDeviceResult write_register(void *ctx, uint8_t reg, const uint8_t *data, size_t count)
{
    if (!ctx)
        return JELLI_DEVICE_UNAVAILABLE;
    if (!data || !count || count > 16u)
        return JELLI_DEVICE_INVALID;
    uint8_t bytes[17] = {reg};
    memcpy(bytes + 1, data, count);
    esp_err_t result = i2c_master_transmit(ctx, bytes, count + 1u, 20);
    return result == ESP_FAIL ? JELLI_DEVICE_IO : result;
}

static uint64_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time() / 1000u;
}

static void wait_ms(void *ctx, unsigned ms)
{
    (void)ctx;
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}

JelliRegisterIo jelli_esp_register_io(i2c_master_dev_handle_t device)
{
    return (JelliRegisterIo){device, read_register, write_register, now_ms, wait_ms};
}

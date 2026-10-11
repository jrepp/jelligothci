#include "commands.h"
#include "suite.h"
#include "debug_usb.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static JelliDebugProtocol protocol;

static void feed(JelliDebugProtocol *wire, void *ctx, char byte, uint64_t now)
{
    (void)ctx;
    (void)now;
    jelli_debug_protocol_feed(wire, byte, jelli_factory_command, wire);
}

void app_main(void)
{
    jelli_debug_usb_init();
    jelli_factory_init();
    ESP_LOGI("factory", "@J1 debug ready; factory schema 2; explicit runs only");
    for (;;) {
        jelli_debug_usb_poll(&protocol, feed, &protocol, (uint64_t)esp_timer_get_time() / 1000u);
        jelli_factory_poll();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

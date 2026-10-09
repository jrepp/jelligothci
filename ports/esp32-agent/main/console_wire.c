#include "console_wire.h"
#include "boot.h"
#include "commands.h"
#include "jelli/agent_console.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

enum { BODY_BYTES = 768, REPLY_BYTES = BODY_BYTES + 32, READ_BYTES = 64 };

static const char *TAG = "agent_console";
/* Owned by the console task alone. */
static JelliAgentConsole console;
static char body[BODY_BYTES], reply[REPLY_BYTES];
static StaticTask_t task_control;
static StackType_t task_stack[6144u / sizeof(StackType_t)];

static void answer(const JelliAgentRequest *request)
{
    jelli_agent_command(request, body, sizeof(body));
    size_t size = jelli_agent_console_reply(reply, sizeof(reply), request->id, body);
    if (!size)
        size = jelli_agent_console_reply(reply, sizeof(reply), request->id,
                                         "{\"ok\":false,\"error\":\"reply_too_large\"}");
    /* One write per reply: the TX ring takes it whole, so logs cannot split it. */
    if (size && usb_serial_jtag_write_bytes(reply, size, pdMS_TO_TICKS(1000)) != (int)size)
        ESP_LOGW(TAG, "Reply %" PRIu32 " was not queued", request->id);
}

static void console_task(void *unused)
{
    (void)unused;
    char bytes[READ_BYTES];
    for (;;) {
        int count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), portMAX_DELAY);
        for (int i = 0; i < count; ++i) {
            JelliAgentRequest request;
            if (!jelli_agent_console_feed(&console, bytes[i], &request))
                continue;
            answer(&request);
            jelli_agent_boot_poll(); /* Restarts here when "boot pet" was accepted. */
        }
    }
}

bool jelli_agent_console_install(void)
{
    /* Startup-only driver allocations: 4 KiB TX, 512 B RX plus SDK metadata. */
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 4096, .rx_buffer_size = 512};
    if (usb_serial_jtag_driver_install(&config) != ESP_OK)
        return false;
    usb_serial_jtag_vfs_use_driver();
    return true;
}

bool jelli_agent_console_run(void)
{
    /* Static buffers: creation cannot fail for lack of memory. */
    if (!xTaskCreateStatic(console_task, "agent_console", sizeof(task_stack), NULL, 4u, task_stack,
                           &task_control))
        return false;
    ESP_LOGI(TAG, "@A1 console ready");
    return true;
}

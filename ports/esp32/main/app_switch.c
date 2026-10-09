#include "app_switch.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include <string.h>

/* Layout A relies on the bootloader's rollback (ADR-013). A generated sdkconfig from
 * before it keeps rollback off without warning, so refuse to build with one. */
#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "Rollback is off: delete the generated sdkconfig and rebuild (docs/hardware.md)"
#endif

/* Must match project() in ports/esp32-agent/CMakeLists.txt. ota_0 can still hold an
 * older vendor app from the board's factory flash, which must never be booted. */
#define AGENT_PROJECT "jelli_agent"

static const char *TAG = "app_switch";
static const esp_partition_t *armed;

bool jelli_esp_app_switch_arm(void *ctx)
{
    (void)ctx;
    const esp_partition_t *agent =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    esp_app_desc_t description;
    if (!agent || esp_ota_get_partition_description(agent, &description) != ESP_OK ||
        strncmp(description.project_name, AGENT_PROJECT, sizeof(description.project_name)) != 0) {
        ESP_LOGW(TAG, "No agent app in ota_0; the pet keeps running");
        return false;
    }
    armed = agent;
    ESP_LOGI(TAG, "Agent %.32s armed; restarting after the reply and a checkpoint",
             description.version);
    return true;
}

void jelli_esp_app_switch_poll(bool reply_pending)
{
    if (!armed || reply_pending)
        return;
    /* Let the reply reach the host before the USB device disappears. */
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(200));
    esp_err_t result = esp_ota_set_boot_partition(armed);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Agent image rejected (%s); the pet keeps running", esp_err_to_name(result));
        armed = NULL;
        return;
    }
    esp_restart();
}

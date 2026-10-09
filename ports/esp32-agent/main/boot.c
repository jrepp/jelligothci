#include "boot.h"
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

/* Must match project() in ports/esp32/CMakeLists.txt. */
#define PET_PROJECT "jelligotchi"

static const char *TAG = "agent_boot";
static const esp_partition_t *armed;

void jelli_agent_boot_confirm(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY)
        return; /* Already confirmed, or selected by a host tool without a trial. */
    esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result == ESP_OK)
        ESP_LOGI(TAG, "Agent start confirmed; it stays selected across resets");
    else
        ESP_LOGW(TAG, "Could not confirm the agent (%s); a reset returns to the pet",
                 esp_err_to_name(result));
}

bool jelli_agent_boot_pet_arm(void)
{
    const esp_partition_t *pet =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    esp_app_desc_t description;
    if (!pet || esp_ota_get_partition_description(pet, &description) != ESP_OK ||
        strncmp(description.project_name, PET_PROJECT, sizeof(description.project_name)) != 0) {
        ESP_LOGW(TAG, "No pet app in factory; the agent keeps running");
        return false;
    }
    armed = pet;
    return true;
}

void jelli_agent_boot_poll(void)
{
    if (!armed)
        return;
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(200));
    /* Selecting factory erases otadata, so the bootloader falls back to the pet. */
    esp_err_t result = esp_ota_set_boot_partition(armed);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not select the pet (%s); the agent keeps running",
                 esp_err_to_name(result));
        armed = NULL;
        return;
    }
    esp_restart();
}

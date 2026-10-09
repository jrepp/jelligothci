#include "settings.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

#define PARTITION "agent_nvs"
#define NAMESPACE "agent"
/* Where the pet's save lives (ports/esp32/partitions.csv); never erased here. */
#define PET_NVS_OFFSET 0x9000u

static const char *TAG = "agent_settings";
static bool ready;

/* agent_nvs can hold leftovers of the vendor firmware that once used that flash, or
 * a newer NVS format; then only this partition is cleared. */
static bool erase_own_partition(void)
{
    const esp_partition_t *own = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, PARTITION);
    if (!own || own->address == PET_NVS_OFFSET)
        return false;
    return nvs_flash_erase_partition(PARTITION) == ESP_OK;
}

bool jelli_agent_settings_open(void)
{
    esp_err_t result = nvs_flash_init_partition(PARTITION);
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, PARTITION " unreadable (%s); clearing it, the pet's save is elsewhere",
                 esp_err_to_name(result));
        if (erase_own_partition())
            result = nvs_flash_init_partition(PARTITION);
    }
    ready = result == ESP_OK;
    if (!ready)
        ESP_LOGE(TAG, PARTITION " unavailable: %s", esp_err_to_name(result));
    return ready;
}

static bool open_namespace(nvs_open_mode_t mode, nvs_handle_t *handle)
{
    return ready && nvs_open_from_partition(PARTITION, NAMESPACE, mode, handle) == ESP_OK;
}

bool jelli_agent_settings_get(const char *key, void *out, size_t *size)
{
    nvs_handle_t handle;
    if (!open_namespace(NVS_READONLY, &handle))
        return false;
    bool ok = nvs_get_blob(handle, key, out, size) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool jelli_agent_settings_set(const char *key, const void *data, size_t size)
{
    nvs_handle_t handle;
    if (!open_namespace(NVS_READWRITE, &handle))
        return false;
    bool ok = nvs_set_blob(handle, key, data, size) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool jelli_agent_settings_erase(const char *key)
{
    nvs_handle_t handle;
    if (!open_namespace(NVS_READWRITE, &handle))
        return false;
    esp_err_t result = nvs_erase_key(handle, key);
    bool ok = (result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

#include "network_internal.h"
#include "nvs.h"

bool jelli_network_load(JelliNetworkConfig *config)
{
    nvs_handle_t handle;
    jelli_network_defaults(config);
    if (nvs_open("jelli_net", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    uint8_t bytes[JELLI_NETWORK_CONFIG_BYTES];
    size_t size = sizeof(bytes);
    esp_err_t result = nvs_get_blob(handle, "config", bytes, &size);
    bool ok = result == ESP_ERR_NVS_NOT_FOUND ||
              (result == ESP_OK && jelli_network_config_decode(config, bytes, size));
    nvs_close(handle);
    return ok; /* Unsupported data is preserved; networking stays unavailable. */
}

esp_err_t jelli_network_store(const JelliNetworkConfig *config)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open("jelli_net", NVS_READWRITE, &handle);
    if (result != ESP_OK)
        return result;
    uint8_t bytes[JELLI_NETWORK_CONFIG_BYTES];
    jelli_network_config_encode(config, bytes);
    result = nvs_set_blob(handle, "config", bytes, sizeof(bytes));
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

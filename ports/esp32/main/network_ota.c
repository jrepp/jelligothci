#include "network_internal.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static esp_err_t download(esp_http_client_handle_t client, const esp_partition_t *partition,
                          esp_ota_handle_t ota)
{
    uint8_t buffer[1024];
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + INT64_C(180000000);
    while (esp_timer_get_time() < deadline) {
        int count = esp_http_client_read(client, (char *)buffer, sizeof(buffer));
        if (count < 0)
            return ESP_FAIL;
        if (!count)
            return received && esp_http_client_is_complete_data_received(client) ? ESP_OK
                                                                                 : ESP_FAIL;
        if ((size_t)count > partition->size - received)
            return ESP_ERR_INVALID_SIZE;
        esp_err_t result = esp_ota_write(ota, buffer, (size_t)count);
        if (result != ESP_OK)
            return result;
        received += (size_t)count;
        vTaskDelay(1); /* No engine/display lock is held while downloading or writing flash. */
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t stage(esp_http_client_handle_t client)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition)
        return ESP_ERR_NOT_FOUND;
    esp_ota_handle_t ota;
    esp_err_t result = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (result != ESP_OK)
        return result;
    result = download(client, partition, ota);
    if (result != ESP_OK) {
        (void)esp_ota_abort(ota);
        return result;
    }
    result = esp_ota_end(ota); /* Checks complete image, chip compatibility and image digest. */
    esp_app_desc_t incoming;
    if (result == ESP_OK)
        result = esp_ota_get_partition_description(partition, &incoming);
    if (result == ESP_OK && memcmp(incoming.project_name, esp_app_get_description()->project_name,
                                   sizeof(incoming.project_name)))
        result = ESP_ERR_INVALID_VERSION;
    if (result == ESP_OK)
        result = esp_ota_set_boot_partition(partition);
    return result;
}

esp_err_t jelli_network_ota(const char *url)
{
    /* Require a plausible UTC clock; never bypass certificate or hostname checks. */
    if (strncmp(url, "https://", 8u) || time(NULL) < (time_t)1735689600)
        return ESP_ERR_INVALID_STATE;
    esp_http_client_config_t http = {.url = url,
                                     .crt_bundle_attach = esp_crt_bundle_attach,
                                     .timeout_ms = 5000,
                                     .disable_auto_redirect = true};
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (!client)
        return ESP_ERR_NO_MEM;
    esp_err_t result = esp_http_client_open(client, 0);
    if (result == ESP_OK) {
        int64_t length = esp_http_client_fetch_headers(client);
        /* Direct 200 responses only: redirects cannot downgrade TLS or loop indefinitely. */
        if (length < 0 || esp_http_client_get_status_code(client) != 200)
            result = ESP_FAIL;
        else
            result = stage(client);
    }
    (void)esp_http_client_close(client);
    (void)esp_http_client_cleanup(client);
    return result;
}

#ifndef TEST_NVS_H
#define TEST_NVS_H
#include <stddef.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef struct {
    size_t available_entries;
} nvs_stats_t;
enum {
    ESP_OK = 0,
    ESP_ERR_NVS_NOT_FOUND = 1,
    ESP_ERR_INVALID_RESPONSE = 2,
    ESP_ERR_NVS_NOT_ENOUGH_SPACE = 3,
    ESP_ERR_INVALID_ARG = 4,
    NVS_READWRITE = 1
};
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_get_stats(const char *partition, nvs_stats_t *stats);
#endif

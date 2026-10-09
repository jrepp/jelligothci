#include "network_internal.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include <string.h>

enum { CONNECTED = 1u };
/* SDK callback shares only RTOS mailboxes, never engine or worker-owned config. */
static QueueHandle_t time_queue;

/* esp_sntp_time_cb_t requires a mutable timeval pointer; this callback only reads it. */
// cppcheck-suppress constParameterCallback
static void time_received(struct timeval *tv)
{
    if (tv->tv_sec < (time_t)946684800 || tv->tv_sec >= (time_t)4102444800)
        return;
    JelliNetworkTime sample = {.seconds = (uint64_t)tv->tv_sec,
                               .received_ms = (uint64_t)esp_timer_get_time() / 1000u};
    (void)xQueueOverwrite(time_queue, &sample);
}

static void wifi_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)data;
    JelliNetworkWorker *worker = ctx;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        (void)xEventGroupSetBits(worker->events, CONNECTED);
    else if ((base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) ||
             (base == WIFI_EVENT &&
              (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)))
        (void)xEventGroupClearBits(worker->events, CONNECTED);
}

static esp_err_t wifi_init(JelliNetworkWorker *worker)
{
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK)
        return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
        return result;
    if (!esp_netif_create_default_wifi_sta())
        return ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&config);
    if (result != ESP_OK)
        return result;
    result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, worker);
    if (result == ESP_OK)
        result = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, worker);
    if (result == ESP_OK)
        result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result == ESP_OK)
        result = esp_wifi_set_mode(WIFI_MODE_STA);
    worker->wifi_ready = result == ESP_OK;
    return result;
}

static esp_err_t configure(JelliNetworkWorker *worker)
{
    if (worker->sntp_ready) {
        esp_netif_sntp_deinit();
        worker->sntp_ready = false;
    }
    if (worker->wifi_started) {
        esp_err_t stopped = esp_wifi_stop();
        if (stopped != ESP_OK)
            return stopped;
        worker->wifi_started = false;
    }
    (void)xEventGroupClearBits(worker->events, CONNECTED);
    if (!worker->state.active.enabled)
        return ESP_OK;
    if (!worker->wifi_ready)
        return ESP_ERR_INVALID_STATE;
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, worker->state.active.ssid, strlen(worker->state.active.ssid));
    memcpy(wifi.sta.password, worker->state.active.password, strlen(worker->state.active.password));
    wifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi.sta.pmf_cfg.capable = true;
    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (result == ESP_OK)
        result = esp_wifi_start();
    worker->wifi_started = result == ESP_OK;
    if (result == ESP_OK)
        result = esp_wifi_connect();
    return result;
}

static void publish(JelliNetworkWorker *worker)
{
    worker->state.connected = (xEventGroupGetBits(worker->events) & CONNECTED) != 0;
    worker->state.free_internal = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    worker->state.minimum_internal = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    worker->state.stack_free = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    (void)xQueueOverwrite(worker->status, &worker->state);
}

static void request(JelliNetworkWorker *worker, const JelliNetworkRequest *req)
{
    if (req->operation == JELLI_NET_APPLY) {
        worker->state.result = jelli_network_store(&req->config);
        if (worker->state.result == ESP_OK) {
            /* SNTP borrows the server string: stop it before replacing active storage. */
            if (worker->sntp_ready) {
                esp_netif_sntp_deinit();
                worker->sntp_ready = false;
            }
            worker->state.active = req->config;
            worker->state.result = configure(worker);
        }
    } else if (!worker->state.connected || worker->state.ota == JELLI_OTA_STAGED) {
        worker->state.result = ESP_ERR_INVALID_STATE;
    } else {
        worker->state.ota = JELLI_OTA_DOWNLOADING;
        publish(worker);
        worker->state.result = jelli_network_ota(worker->state.active.ota_url);
        worker->state.ota = worker->state.result == ESP_OK ? JELLI_OTA_STAGED : JELLI_OTA_FAILED;
    }
    worker->state.completed = req->id;
}

void jelli_network_worker(void *ctx)
{
    JelliNetworkWorker *worker = ctx;
    time_queue = worker->time;
    worker->state.result = wifi_init(worker);
    if (worker->state.result == ESP_OK)
        worker->state.result = configure(worker);
    uint64_t retry_ms = 0;
    for (;;) {
        publish(worker);
        JelliNetworkRequest req;
        if (xQueueReceive(worker->requests, &req, pdMS_TO_TICKS(250)) == pdTRUE) {
            request(worker, &req);
            memset(&req, 0, sizeof(req));
        }
        bool connected = (xEventGroupGetBits(worker->events) & CONNECTED) != 0;
        if (connected && !worker->sntp_ready) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(worker->state.active.ntp);
            config.sync_cb = time_received;
            config.wait_for_sync = false;
            esp_sntp_set_sync_interval(worker->state.active.sync_seconds * 1000u);
            esp_err_t result = esp_netif_sntp_init(&config);
            worker->sntp_ready = result == ESP_OK;
            if (result != ESP_OK)
                worker->state.result = result;
        }
        uint64_t now = (uint64_t)esp_timer_get_time() / 1000u;
        if (worker->wifi_started && !connected && now >= retry_ms) {
            (void)esp_wifi_connect();
            retry_ms = now + 15000u;
        }
    }
}

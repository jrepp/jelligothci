#ifndef JELLI_NETWORK_INTERNAL_H
#define JELLI_NETWORK_INTERNAL_H
#include "network_config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

typedef enum { JELLI_NET_APPLY, JELLI_NET_OTA } JelliNetworkOperation;
typedef enum {
    JELLI_OTA_IDLE,
    JELLI_OTA_DOWNLOADING,
    JELLI_OTA_STAGED,
    JELLI_OTA_FAILED
} JelliOtaState;
typedef struct {
    JelliNetworkConfig config;
    uint32_t id;
    JelliNetworkOperation operation;
} JelliNetworkRequest;
typedef struct {
    JelliNetworkConfig active;
    uint32_t completed;
    uint32_t free_internal, minimum_internal, stack_free;
    esp_err_t result;
    JelliOtaState ota;
    bool connected;
} JelliNetworkStatus;
typedef struct {
    uint64_t seconds, received_ms;
} JelliNetworkTime;
typedef struct {
    QueueHandle_t requests, status, time;
    EventGroupHandle_t events;
    JelliNetworkStatus state;
    bool wifi_ready, wifi_started, sntp_ready;
} JelliNetworkWorker;

_Static_assert(sizeof(JelliNetworkRequest) <= 512u, "Network request exceeds 512 bytes");
_Static_assert(sizeof(JelliNetworkStatus) <= 512u, "Network status exceeds 512 bytes");

bool jelli_network_load(JelliNetworkConfig *config);
esp_err_t jelli_network_store(const JelliNetworkConfig *config);
void jelli_network_worker(void *ctx);
esp_err_t jelli_network_ota(const char *url);
#endif

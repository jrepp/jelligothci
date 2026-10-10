#include "wifi.h"
#include "settings.h"
#include "wifi_internal.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Either option would make the driver open the default nvs partition, the pet's
 * save (ADR-013). A stale generated sdkconfig keeps them on, so refuse it. */
#if defined(CONFIG_ESP_WIFI_NVS_ENABLED) || defined(CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE)
#error "Wi-Fi would use the pet's nvs: delete the generated sdkconfig and rebuild"
#endif

/* GAVE_UP: the retries after a disconnect ran out. A join tries twice, briefly. */
enum { CONNECTED = 1 << 0, GAVE_UP = 1 << 1, RETRIES = 5, JOIN_RETRIES = 2, JOIN_MS = 15000 };

static const char *TAG = "agent_wifi";
static EventGroupHandle_t events;
static StaticEventGroup_t events_storage;
/* One caller at a time: the console's commands and Settings' Wi-Fi task. */
static SemaphoreHandle_t lock;
static StaticSemaphore_t lock_storage;
static esp_netif_t *station;
static bool started;
/* Shared by the event task (decrements) and the callers (reset). */
static atomic_uint retries_left;
/* Why the station last lost or failed a connection (WIFI_REASON_*). */
static atomic_uint last_reason;
/* Set just before this side's own disconnect, so its "leaving" (reason 8) is not
 * retried; the same reason from the AP, real or forged, is. Cleared on associating. */
static atomic_bool leaving;
/* Set while the driver's network is being changed: disconnects then are this side's
 * doing, and nothing retries, so no stale event can reconnect to the old network. */
static atomic_bool configuring;
/* Whether the station should be online: a network is configured and not forgotten. */
static atomic_bool wanted;
/* While a join runs, the slow reconnect leaves the radio to it. */
static atomic_bool joining;
/* Once the quick retries give up (the router restarting, say), the station tries the
 * stored network again every AGAIN_US, for as long as it is offline and wants to be.
 * The timer only wakes a small task of its own, which does the work: esp_timer
 * callbacks must not block. */
static esp_timer_handle_t again;
static TaskHandle_t again_task;
enum { AGAIN_US = 30000000, SETTLE_MS = 100 };

static void arm_again(void)
{
    if (again && atomic_load(&wanted) && !atomic_load(&joining) && !esp_timer_is_active(again))
        (void)esp_timer_start_once(again, AGAIN_US);
}

static void on_disconnect(const wifi_event_sta_disconnected_t *gone)
{
    xEventGroupClearBits(events, CONNECTED);
    bool ours = gone && gone->reason == WIFI_REASON_ASSOC_LEAVE;
    if (atomic_load(&configuring)) { /* A change is under way; it connects by itself. */
        if (ours)
            atomic_store(&leaving, false);
        return;
    }
    if (ours && atomic_exchange(&leaving, false)) {
        arm_again(); /* This side left; should nothing follow, the slow retry still will. */
        return;
    }
    atomic_store(&last_reason, gone ? gone->reason : 0u);
    if (!atomic_load(&wanted))
        return;
    /* A few quick retries, then the slow ones, so a wrong password cannot keep the
     * radio busy and a router that restarts is found again. */
    unsigned left = atomic_load(&retries_left);
    /* A retry the driver refused brings no event: then the slow retry takes over. */
    if (left && atomic_compare_exchange_strong(&retries_left, &left, left - 1u) &&
        esp_wifi_connect() == ESP_OK)
        return;
    xEventGroupSetBits(events, GAVE_UP);
    arm_again();
}

/* An esp_event_handler_t: its data is void *, though only read here. */
// cppcheck-suppress constParameterCallback
static void on_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        on_disconnect(data);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        atomic_store(&leaving, false); /* Any leave of ours came before this. */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&leaving, false);
        atomic_store(&retries_left, RETRIES);
        if (again)
            (void)esp_timer_stop(again);
        xEventGroupSetBits(events, CONNECTED);
    }
}

static bool connect_stored(void);

static void wake_again(void *unused)
{
    (void)unused;
    if (again_task)
        xTaskNotifyGive(again_task);
}

/* The slow reconnect: the stored network, loaded afresh, so no stale driver
 * configuration is ever retried. While the station stays wanted and offline the next
 * one is armed whatever happened; an address stops it. A join has the radio itself. */
static void again_loop(void *unused)
{
    (void)unused;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (atomic_load(&joining) || !atomic_load(&wanted) || jelli_agent_wifi_wait(0u))
            continue;
        xSemaphoreTake(lock, portMAX_DELAY);
        if (!atomic_load(&joining) && atomic_load(&wanted)) /* As it may be after waiting. */
            (void)connect_stored();
        xSemaphoreGive(lock);
        arm_again();
    }
}

static void leave(void)
{
    atomic_store(&leaving, true);
    (void)esp_wifi_disconnect();
}

/* Sets the driver's network. While the station is still busy with the old one the
 * driver refuses; then it is told to leave again and given a moment, once. */
static bool configure(wifi_config_t *config)
{
    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, config);
    if (result == ESP_ERR_WIFI_STATE) {
        leave();
        vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
        result = esp_wifi_set_config(WIFI_IF_STA, config);
    }
    return result == ESP_OK;
}

/* Disconnects and empties the driver's configuration, so no password stays in its
 * RAM; whether a network is still wanted is the caller's. Lock held. */
static void empty_driver(void)
{
    atomic_store(&configuring, true);
    leave();
    wifi_config_t none = {0};
    if (!configure(&none))
        ESP_LOGW(TAG, "The driver kept its last network's configuration");
    atomic_store(&configuring, false);
}

/* No network wanted any more: nothing reconnects. Lock held. */
static void forget(void)
{
    atomic_store(&wanted, false);
    if (again)
        (void)esp_timer_stop(again);
    empty_driver();
}

/* Clears the copy of a password the driver's configuration held on this stack. */
static void wipe(wifi_config_t *config)
{
    volatile uint8_t *bytes = (volatile uint8_t *)config;
    for (size_t i = 0u; i < sizeof(*config); ++i)
        bytes[i] = 0u;
}

/* Connects with config (its ssid and password already set), after retries reset. No
 * disconnect during the change retries anything. Lock held. */
static bool connect_with(wifi_config_t *config, size_t password_size, unsigned retries)
{
    config->sta.threshold.authmode = password_size ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    config->sta.pmf_cfg.capable = true;
    atomic_store(&configuring, true);
    leave();
    xEventGroupClearBits(events, CONNECTED | GAVE_UP);
    atomic_store(&retries_left, retries);
    atomic_store(&last_reason, 0u);
    bool ok = configure(config);
    wipe(config); /* The driver keeps its own copy. */
    if (ok)       /* A refused change leaves the driver, and so the intent, as they were. */
        atomic_store(&wanted, true);
    atomic_store(&configuring, false);
    return ok && esp_wifi_connect() == ESP_OK;
}

/* Loads stored credentials into the driver and connects; false when none are stored.
 * Lock held. */
static bool connect_stored(void)
{
    wifi_config_t config = {0};
    size_t ssid_size = sizeof(config.sta.ssid), password_size = sizeof(config.sta.password);
    if (!jelli_agent_settings_get("ssid", config.sta.ssid, &ssid_size) || !ssid_size)
        return false;
    if (!jelli_agent_settings_get("pass", config.sta.password, &password_size))
        password_size = 0u;
    return connect_with(&config, password_size, RETRIES);
}

bool jelli_agent_wifi_start(void)
{
    events = xEventGroupCreateStatic(&events_storage);
    lock = xSemaphoreCreateMutexStatic(&lock_storage);
    static StaticTask_t again_control;
    static StackType_t again_stack[3072u / sizeof(StackType_t)];
    again_task = xTaskCreateStatic(again_loop, "wifi_again", sizeof(again_stack), NULL, 2u,
                                   again_stack, &again_control);
    const esp_timer_create_args_t timer = {.callback = wake_again, .name = "wifi_again"};
    if (!again_task || esp_timer_create(&timer, &again) != ESP_OK) /* Without it, no slow retry. */
        again = NULL;
    esp_err_t loop = esp_event_loop_create_default();
    if (esp_netif_init() != ESP_OK || (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE))
        return false;
    station = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (!station || esp_wifi_init(&init) != ESP_OK ||
        esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK)
        return false;
    started = true;
    jelli_agent_wifi_power_save(true);
    if (!connect_stored())
        ESP_LOGI(TAG, "No Wi-Fi credentials yet; use Settings or scripts/loki-agent wifi set");
    arm_again(); /* A stored network that does not answer at start is retried. */
    return true;
}

/* Whether a network is stored. */
static bool stored(void)
{
    size_t size = 0u;
    return jelli_agent_settings_get("ssid", NULL, &size) && size;
}

/* Stores credentials for the next start. Lock held. */
static bool store(const uint8_t *ssid, size_t ssid_size, const void *password, size_t password_size)
{
    return jelli_agent_settings_set("ssid", ssid, ssid_size) &&
           (password_size ? jelli_agent_settings_set("pass", password, password_size)
                          : jelli_agent_settings_erase("pass"));
}

static bool valid(size_t ssid_size, size_t password_size)
{
    return ssid_size && ssid_size <= 32u &&
           (!password_size || (password_size >= 8u && password_size <= 63u));
}

bool jelli_agent_wifi_set(const uint8_t *ssid, size_t ssid_size, const uint8_t *password,
                          size_t password_size)
{
    if (!started || !valid(ssid_size, password_size))
        return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = store(ssid, ssid_size, password, password_size) && connect_stored();
    arm_again(); /* Should this first attempt fail, the slow retry follows. */
    xSemaphoreGive(lock);
    return ok;
}

bool jelli_agent_wifi_clear(void)
{
    if (!started)
        return jelli_agent_settings_erase("ssid") && jelli_agent_settings_erase("pass");
    xSemaphoreTake(lock, portMAX_DELAY);
    forget();
    bool ok = jelli_agent_settings_erase("ssid") && jelli_agent_settings_erase("pass");
    xSemaphoreGive(lock);
    return ok;
}

bool jelli_agent_wifi_take(void)
{
    if (!started)
        return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    return true;
}

void jelli_agent_wifi_give(void) { xSemaphoreGive(lock); }

bool jelli_agent_wifi_started(void) { return started; }

esp_netif_t *jelli_agent_wifi_station(void) { return station; }

bool jelli_agent_wifi_wait(uint32_t ms)
{
    return started && (xEventGroupWaitBits(events, CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(ms)) &
                       CONNECTED) != 0;
}

void jelli_agent_wifi_power_save(bool on)
{
    if (started)
        (void)esp_wifi_set_ps(on ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

/* Whether the station is associated with this network now. */
static bool joined_to(const uint8_t *ssid, size_t ssid_size)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
           strnlen((const char *)ap.ssid, sizeof(ap.ssid)) == ssid_size &&
           !memcmp(ap.ssid, ssid, ssid_size);
}

/* How a failed join reads to the user, from the station's last reason. */
static JelliPrefsOutcome outcome_of(unsigned reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return JELLI_PREFS_WRONG_PASSWORD;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return JELLI_PREFS_NOT_FOUND;
    default:
        return JELLI_PREFS_FAILED;
    }
}

JelliPrefsOutcome jelli_agent_wifi_join(const uint8_t *ssid, size_t ssid_size, const char *password,
                                        size_t password_size)
{
    if (!started || !ssid || (password_size && !password) || !valid(ssid_size, password_size))
        return JELLI_PREFS_FAILED;
    xSemaphoreTake(lock, portMAX_DELAY);
    atomic_store(&joining, true);
    if (again)
        (void)esp_timer_stop(again);
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, ssid_size);
    memcpy(config.sta.password, password, password_size);
    EventBits_t bits = 0;
    if (connect_with(&config, password_size, JOIN_RETRIES))
        bits = xEventGroupWaitBits(events, CONNECTED | GAVE_UP, pdFALSE, pdFALSE,
                                   pdMS_TO_TICKS(JOIN_MS));
    /* Only an address on the network asked for counts, not a late one from before. */
    bool joined = (bits & CONNECTED) && joined_to(ssid, ssid_size);
    JelliPrefsOutcome outcome = joined ? JELLI_PREFS_JOINED : outcome_of(atomic_load(&last_reason));
    if (!joined) /* No stray event retries the failed network from here on. */
        atomic_store(&retries_left, 0u);
    atomic_store(&joining, false);
    if (joined) {
        atomic_store(&retries_left, RETRIES);
        if (!store(ssid, ssid_size, password, password_size))
            ESP_LOGW(TAG, "Joined, but the network could not be stored for the next start");
    } else if (!stored()) {
        forget(); /* Nothing to go back to: stop trying the new one, and drop it. */
    } else {
        empty_driver(); /* The failed network's password leaves the driver first. */
        (void)connect_stored();
        arm_again(); /* Until it is back, the slow retry reloads it. */
    }
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "Join: outcome %d (reason %u)", (int)outcome, atomic_load(&last_reason));
    return outcome;
}

#include "loki_link.h"
#include "identity.h"
#include "loki_link_rx.h"
#include "loki_models.h"
#include "loki_peer.h"
#include "pairing.h"
#include "wifi.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum {
    CONNECT_TIMEOUT_MS = 10000, /* TCP, TLS with ECDHE-ECDSA both ways, and the upgrade. */
    SEND_TIMEOUT_MS = 1000,
    BACKOFF_MIN_MS = 1000,
    BACKOFF_MAX_MS = 30000,
    STEADY_US = 60 * 1000000, /* A session this long resets the backoff. */
    STATUS_PERIOD_US = 30 * 1000000,
    INBOX_DEPTH = 8
};

static const char *TAG = "loki_link";
static EventGroupHandle_t bits;
static StaticEventGroup_t bits_storage;
static SemaphoreHandle_t send_lock;
static StaticSemaphore_t send_lock_storage;
static QueueHandle_t inbox;
/* The session's client and transports: made by the link task before the session
 * starts, used by senders under send_lock while it is online, and freed by the link
 * task under send_lock once the client's task has finished. */
static esp_websocket_client_handle_t client;
static esp_transport_handle_t ssl_transport, ws_transport;
static bool session_started;    /* Link task only. */
static uint32_t tx_seq;         /* Guarded by send_lock. */
static JelliAgentBridge bridge; /* Link task only. */
static char dial[16];           /* Link task only: the address being dialled. */
static atomic_uint sessions, failures;
/* Written by several tasks, so copied under a spinlock. */
static char last_error[48];
static portMUX_TYPE last_error_lock = portMUX_INITIALIZER_UNLOCKED;

static void set_last_error(const char *why)
{
    char copy[sizeof(last_error)];
    (void)snprintf(copy, sizeof(copy), "%s", why);
    taskENTER_CRITICAL(&last_error_lock);
    memcpy(last_error, copy, sizeof(last_error));
    taskEXIT_CRITICAL(&last_error_lock);
}

static void get_last_error(char out[48])
{
    taskENTER_CRITICAL(&last_error_lock);
    memcpy(out, last_error, sizeof(last_error));
    taskEXIT_CRITICAL(&last_error_lock);
}

void jelli_agent_link_set(unsigned bits_to_set)
{
    xEventGroupSetBits(bits, (EventBits_t)bits_to_set);
}

/* The first reason a session ended is the one kept for status. */
void jelli_agent_link_closed(const char *why)
{
    if (!(xEventGroupGetBits(bits) & JELLI_LINK_CLOSED))
        set_last_error(why);
    xEventGroupClearBits(bits, JELLI_LINK_WELCOMED);
    xEventGroupSetBits(bits, JELLI_LINK_CLOSED);
    jelli_agent_models_forget(); /* Only a connected bridge's list can be picked from. */
}

/* esp_websocket_client keeps reading a frame or a handshake for as long as bytes
 * trickle in, holding its lock, so a stalled or hostile peer could wedge every send
 * and the teardown. Shutting the socket down makes the read fail at once and the
 * client end the session (memo-025, memo-027). */
void jelli_agent_link_abort(const char *why)
{
    ESP_LOGW(TAG, "Ending the session: %s", why);
    jelli_agent_link_closed(why);
    jelli_agent_peer_shutdown();
}

bool jelli_agent_link_deliver(const JelliLokiMessage *message)
{
    if (xQueueSend(inbox, message, 0) == pdTRUE)
        return true;
    ESP_LOGW(TAG, "App inbox full; message %" PRIu32 " dropped", message->seq);
    return false;
}

/* A failed send ends the session from inside the sending task (the client's error
 * events run there), so a handler that tried to send would wait on a lock its own
 * task holds. Such a send is refused instead (memo-027). */
static bool holding_send_lock(void)
{
    return xSemaphoreGetMutexHolder(send_lock) == xTaskGetCurrentTaskHandle();
}

/* hello goes out from the WebSocket task as the connection opens; everything else
 * needs the bridge's welcome first. A failed send aborts under send_lock, so the
 * abort cannot outlive its session (loki_peer.h). */
static bool send_message(JelliLokiOut *message, bool before_welcome)
{
    char text[JELLI_LOKI_MAX_OUT + 1u];
    if (holding_send_lock() || (!before_welcome && !jelli_agent_link_online()))
        return false;
    xSemaphoreTake(send_lock, portMAX_DELAY);
    bool live = (before_welcome || jelli_agent_link_online()) && client;
    message->seq = tx_seq + 1u;
    size_t size = live ? jelli_loki_encode(message, text, sizeof(text)) : 0u;
    bool sent = size && esp_websocket_client_send_text(client, text, (int)size,
                                                       pdMS_TO_TICKS(SEND_TIMEOUT_MS)) == (int)size;
    if (sent)
        ++tx_seq;
    else if (size)
        jelli_agent_link_abort("send failed");
    xSemaphoreGive(send_lock);
    return sent;
}

bool jelli_agent_link_send(JelliLokiOut *message)
{
    return message->type != JELLI_LOKI_HELLO && send_message(message, false);
}

bool jelli_agent_link_send_audio(const JelliLokiAudio *audio, const int16_t *samples, size_t count)
{
    size_t size = count * sizeof(int16_t);
    if (!size || size > JELLI_LOKI_AUDIO_MAX || holding_send_lock() || !jelli_agent_link_online())
        return false;
    xSemaphoreTake(send_lock, portMAX_DELAY);
    bool live = jelli_agent_link_online() && client, sent = false;
    if (live) {
        static uint8_t frame[JELLI_LOKI_AUDIO_HEADER + JELLI_LOKI_AUDIO_MAX]; /* Under send_lock. */
        jelli_loki_audio_header(audio, frame);
        memcpy(frame + JELLI_LOKI_AUDIO_HEADER, samples, size);
        int length = (int)(JELLI_LOKI_AUDIO_HEADER + size);
        sent = esp_websocket_client_send_bin(client, (const char *)frame, length,
                                             pdMS_TO_TICKS(SEND_TIMEOUT_MS)) == length;
        if (!sent)
            jelli_agent_link_abort("audio send failed");
    }
    xSemaphoreGive(send_lock);
    return sent;
}

void jelli_agent_link_send_hello(void)
{
    xSemaphoreTake(send_lock, portMAX_DELAY);
    tx_seq = 0u;
    xSemaphoreGive(send_lock);
    JelliLokiOut hello = {.type = JELLI_LOKI_HELLO,
                          .fw = JELLI_VERSION_LABEL,
                          .board = JELLI_BOARD_ID,
                          .channels = 1u,
                          .frame_ms = 60u};
    if (!send_message(&hello, true))
        jelli_agent_link_abort("hello failed");
}

bool jelli_agent_link_online(void)
{
    return bits && (xEventGroupGetBits(bits) & JELLI_LINK_WELCOMED) != 0;
}

bool jelli_agent_link_receive(JelliLokiMessage *message, uint32_t wait_ms)
{
    return inbox && xQueueReceive(inbox, message, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

/* SSL under WebSocket, owned here so a stalled read can be ended (abort) and the
 * client does not free them. With an external transport the client applies none of
 * its own transport settings, so path, subprotocol, and control frames are set here:
 * the client must see pongs, or it drops a healthy session after its timeout. */
static bool make_transports(void)
{
    esp_transport_keep_alive_t keep_alive = {.keep_alive_enable = true,
                                             .keep_alive_idle = 10,
                                             .keep_alive_interval = 5,
                                             .keep_alive_count = 3};
    esp_transport_ws_config_t ws = {
        .ws_path = "/loki/1", .sub_protocol = "loki.1", .propagate_control_frames = true};
    size_t cert_size = 0u, key_size = 0u;
    const uint8_t *cert = jelli_agent_identity_cert(&cert_size);
    const uint8_t *key = jelli_agent_identity_key(&key_size);
    ssl_transport = cert && key ? esp_transport_ssl_init() : NULL;
    ws_transport = ssl_transport ? esp_transport_ws_init(ssl_transport) : NULL;
    if (!ws_transport || esp_transport_ws_set_config(ws_transport, &ws) != ESP_OK)
        return false;
    /* The bridge's own certificate is the only trust anchor (pairing.h). Names are not
     * checked: the pin is the trust, and the bridge may be reached by address. */
    esp_transport_ssl_set_cert_data_der(ssl_transport, (const char *)bridge.cert,
                                        (int)bridge.cert_size);
    esp_transport_ssl_set_client_cert_data_der(ssl_transport, (const char *)cert, (int)cert_size);
    esp_transport_ssl_set_client_key_data_der(ssl_transport, (const char *)key, (int)key_size);
    esp_transport_ssl_skip_common_name_check(ssl_transport);
    esp_transport_ssl_set_keep_alive(ssl_transport, &keep_alive);
    return true;
}

static void free_transports(void)
{
    if (ws_transport)
        (void)esp_transport_destroy(ws_transport);
    if (ssl_transport)
        (void)esp_transport_destroy(ssl_transport);
    ws_transport = ssl_transport = NULL;
}

/* Teardown that no peer can stall (memo-025, memo-027): a close frame offered with a
 * timeout, then the session's socket shut down, again each second, until the client's
 * task has finished. Every blocked read, write, or handshake fails at once, and a
 * connect still in progress ends by its own timeout. Only then are the client and
 * transports freed, under send_lock so no sender is inside them; destroy no longer
 * waits on the task. */
static void end_session(void)
{
    if (client && session_started) {
        if (xEventGroupGetBits(bits) & JELLI_LINK_CONNECTED)
            (void)esp_websocket_client_send_with_opcode(client, WS_TRANSPORT_OPCODES_CLOSE, NULL, 0,
                                                        pdMS_TO_TICKS(500));
        for (unsigned waited = 0u; !(xEventGroupWaitBits(bits, JELLI_LINK_FINISHED, pdFALSE,
                                                         pdFALSE, pdMS_TO_TICKS(1000)) &
                                     JELLI_LINK_FINISHED);
             ++waited) {
            jelli_agent_peer_shutdown();
            if (waited % 10u == 9u)
                ESP_LOGW(TAG, "The session has not ended after %u s", waited + 1u);
        }
    }
    xSemaphoreTake(send_lock, portMAX_DELAY);
    if (client)
        (void)esp_websocket_client_destroy(client);
    client = NULL;
    session_started = false;
    free_transports();
    jelli_agent_peer_clear();
    xSemaphoreGive(send_lock);
}

static bool open_session(void)
{
    xEventGroupClearBits(bits, JELLI_LINK_CONNECTED | JELLI_LINK_CLOSED | JELLI_LINK_WELCOMED |
                                   JELLI_LINK_FINISHED);
    if (!jelli_agent_peer_set(dial, bridge.port) || !make_transports()) {
        jelli_agent_link_closed("transport");
        end_session();
        return false;
    }
    esp_websocket_client_config_t config = {.host = dial,
                                            .port = bridge.port,
                                            .path = "/loki/1",
                                            .ext_transport = ws_transport,
                                            .buffer_size =
                                                JELLI_LOKI_AUDIO_HEADER + JELLI_LOKI_AUDIO_MAX,
                                            .task_stack = 8192,
                                            .network_timeout_ms = CONNECT_TIMEOUT_MS,
                                            .disable_auto_reconnect = true,
                                            .ping_interval_sec = 10,
                                            .pingpong_timeout_sec = 30};
    jelli_agent_link_rx_reset();
    client = esp_websocket_client_init(&config);
    session_started = client &&
                      esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                                    jelli_agent_link_on_event, NULL) == ESP_OK &&
                      esp_websocket_client_start(client) == ESP_OK;
    bool ok = session_started &&
              (xEventGroupWaitBits(bits, JELLI_LINK_WELCOMED | JELLI_LINK_CLOSED, pdFALSE, pdFALSE,
                                   pdMS_TO_TICKS(CONNECT_TIMEOUT_MS)) &
               JELLI_LINK_WELCOMED);
    if (!ok) {
        jelli_agent_link_closed("no welcome");
        end_session();
    }
    return ok;
}

static void send_status(void)
{
    wifi_ap_record_t ap = {0};
    JelliLokiOut status = {.type = JELLI_LOKI_STATUS, .battery = -1, .charging = false};
    status.rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.rssi < 0 ? ap.rssi : -127;
    (void)jelli_agent_link_send(&status);
}

/* Until the session closes, the pairing changes, or Wi-Fi drops: health every 30 s. */
static void run_session(uint32_t generation)
{
    int64_t next_status = esp_timer_get_time();
    while (!(xEventGroupWaitBits(bits, JELLI_LINK_CLOSED, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000)) &
             JELLI_LINK_CLOSED)) {
        if (jelli_agent_pairing_generation() != generation)
            jelli_agent_link_abort("pairing changed");
        else if (!jelli_agent_wifi_wait(0u))
            jelli_agent_link_abort("Wi-Fi down");
        else if (esp_timer_get_time() >= next_status) {
            send_status();
            next_status = esp_timer_get_time() + STATUS_PERIOD_US;
        }
    }
    end_session();
}

static void link_task(void *unused)
{
    (void)unused;
    uint32_t backoff = BACKOFF_MIN_MS;
    for (;;) {
        uint32_t generation = jelli_agent_pairing_generation();
        if (!jelli_agent_pairing_get(&bridge) || !jelli_agent_wifi_wait(1000u)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int64_t started = esp_timer_get_time();
        if (!jelli_agent_peer_resolve(&bridge, generation, dial)) {
            jelli_agent_link_closed("bridge address unknown");
            atomic_fetch_add(&failures, 1u);
        } else if (open_session()) {
            atomic_fetch_add(&sessions, 1u);
            ESP_LOGI(TAG, "Online with the bridge at %s (%s):%u", bridge.host, dial,
                     (unsigned)bridge.port);
            run_session(generation);
        } else {
            atomic_fetch_add(&failures, 1u);
        }
        char why[sizeof(last_error)];
        get_last_error(why);
        ESP_LOGI(TAG, "Offline: %s", why);
        if (esp_timer_get_time() - started > STEADY_US)
            backoff = BACKOFF_MIN_MS;
        vTaskDelay(pdMS_TO_TICKS(backoff + esp_random() % (backoff / 4u + 1u)));
        backoff = backoff * 2u > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : backoff * 2u;
    }
}

bool jelli_agent_link_start(const JelliAgentPlayerHooks *hooks)
{
    static StaticTask_t task_control;
    static StackType_t task_stack[6144u / sizeof(StackType_t)];
    bits = xEventGroupCreateStatic(&bits_storage);
    send_lock = xSemaphoreCreateMutexStatic(&send_lock_storage);
    /* The app inbox holds whole decoded messages (2 KB each): PSRAM, once. */
    inbox = xQueueCreateWithCaps(INBOX_DEPTH, sizeof(JelliLokiMessage), MALLOC_CAP_SPIRAM);
    if (!bits || !send_lock || !inbox)
        return false;
    jelli_agent_link_rx_hooks(hooks);
    return xTaskCreateStaticPinnedToCore(link_task, "loki_link", sizeof(task_stack), NULL, 5u,
                                         task_stack, &task_control, 0) != NULL;
}

void jelli_agent_link_status(char *out, size_t capacity)
{
    static JelliAgentBridge paired;
    char why[sizeof(last_error)];
    bool is_paired = jelli_agent_pairing_get(&paired);
    get_last_error(why);
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"paired\":%s,\"online\":%s,\"sessions\":%u,\"failures\":%u,"
                   "\"last\":\"%s\"}",
                   is_paired ? "true" : "false", jelli_agent_link_online() ? "true" : "false",
                   atomic_load(&sessions), atomic_load(&failures), why);
}

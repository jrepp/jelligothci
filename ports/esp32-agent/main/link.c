#include "link.h"
#include "audio.h"
#include "wifi.h"
#include "jelli/agent_args.h"
#include "jelli/agent_pcm.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

/* 60 ms frames of [mic, reference] at 24 kHz; playback in 20 ms blocks after a
 * 300 ms lead, from a PSRAM ring buffer that holds about 2.7 s. */
enum {
    FRAME_SAMPLES = JELLI_AGENT_AUDIO_RATE * 60 / 1000,
    PLAY_BLOCK = JELLI_AGENT_AUDIO_RATE * 20 / 1000,
    PLAY_LEAD_BYTES = JELLI_AGENT_AUDIO_RATE * 300 / 1000 * 2,
    PLAY_BUFFER_BYTES = 128 * 1024,
    SEND_TIMEOUT_MS = 200,
    CONNECT_TIMEOUT_MS = 5000,
    DRAIN_TIMEOUT_MS = 15000,
    /* The spike server sends 60 ms chunks (2,880 bytes); anything near this is not it. */
    MAX_MESSAGE_BYTES = 16 * 1024
};
enum {
    CONNECTED = 1 << 0,
    CLOSED = 1 << 1,
    PLAYING = 1 << 2,
    PLAY_ENDED = 1 << 3,
    IDLE = 1 << 4,
    FLUSH = 1 << 5,  /* Console asks the playback task to drop leftovers... */
    FLUSHED = 1 << 6 /* ...and the playback task confirms. */
};

static const char *TAG = "agent_link";
static EventGroupHandle_t state;
static StaticEventGroup_t state_storage;
static RingbufHandle_t playback;
static atomic_uint received_bytes, dropped_bytes, played_bytes;
/* Console task only. */
static int16_t capture[FRAME_SAMPLES * JELLI_AGENT_AUDIO_CHANNELS];
static uint8_t message[JELLI_AGENT_FRAME_HEADER + sizeof(capture)];
/* Websocket task only: a sample's first byte when a network read split it. */
static JelliAgentPcmJoin pcm_join;
/* Created and destroyed by the console task around each session; the websocket
 * task reads it only while that session's client runs. */
static esp_transport_handle_t tcp_transport, ws_transport;

/* Bounded search in a message that is not NUL-terminated. */
static bool contains(const char *data, int size, const char *needle)
{
    size_t length = strlen(needle);
    for (size_t i = 0u; size > 0 && i + length <= (size_t)size; ++i)
        if (!memcmp(data + i, needle, length))
            return true;
    return false;
}

static void queue_pcm(const void *data, size_t size)
{
    atomic_fetch_add(&received_bytes, (unsigned)size);
    if (xRingbufferSend(playback, data, size, 0) != pdTRUE)
        atomic_fetch_add(&dropped_bytes, (unsigned)size);
}

/* Network reads can split a message at any byte (jelli_agent_pcm_join keeps the
 * samples aligned). */
static void on_audio(const esp_websocket_event_data_t *event)
{
    JelliAgentPcmParts parts;
    jelli_agent_pcm_join(&pcm_join, (const uint8_t *)event->data_ptr, (size_t)event->data_len,
                         event->payload_offset == 0 && event->op_code != 0x0, &parts);
    if (parts.joined_size)
        queue_pcm(parts.joined, parts.joined_size);
    if (parts.body_size)
        queue_pcm(parts.body, parts.body_size);
}

/* esp_websocket_client keeps reading a frame's announced payload until it arrives,
 * holding its lock and ignoring stop requests, so a peer that stalls mid-frame (or
 * a link that drops) would wedge every later send, close, and destroy. Shutting
 * the socket down makes the next read fail and the client end the session. */
static void abort_session(const char *why)
{
    int socket = esp_transport_get_socket(ws_transport);
    ESP_LOGI(TAG, "Ending the session: %s", why);
    if (socket >= 0)
        (void)shutdown(socket, SHUT_RDWR);
}

/* Websocket task: queue PCM for the playback task; never block it. */
static void on_data(const esp_websocket_event_data_t *event)
{
    if (event->payload_len > MAX_MESSAGE_BYTES) {
        abort_session("oversized message");
        return;
    }
    if (event->data_len == 0 && event->payload_offset < event->payload_len) {
        abort_session("message stalled"); /* A read timed out inside a frame. */
        return;
    }
    if (event->op_code == 0x1) { /* Text: the spike's control messages. */
        if (contains(event->data_ptr, event->data_len, "\"play_begin\"")) {
            xEventGroupClearBits(state, PLAY_ENDED | IDLE);
            xEventGroupSetBits(state, PLAYING);
        } else if (contains(event->data_ptr, event->data_len, "\"play_end\"")) {
            xEventGroupSetBits(state, PLAY_ENDED);
        }
    } else if ((event->op_code == 0x2 || event->op_code == 0x0) && event->data_len > 0) {
        on_audio(event);
    }
}

/* esp_event_handler_t fixes this signature, so data cannot be a const pointer. */
// cppcheck-suppress constParameterCallback
static void on_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    (void)base;
    if (id == WEBSOCKET_EVENT_CONNECTED)
        xEventGroupSetBits(state, CONNECTED);
    else if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED ||
             id == WEBSOCKET_EVENT_ERROR || id == WEBSOCKET_EVENT_FINISH)
        xEventGroupSetBits(state, CLOSED | PLAY_ENDED); /* Play out what arrived. */
    else if (id == WEBSOCKET_EVENT_DATA)
        on_data(data);
}

/* Drops anything a previous session left, then reports idle. */
static void flush_playback(void)
{
    size_t size = 0u;
    void *block;
    while ((block = xRingbufferReceiveUpTo(playback, &size, 0, PLAY_BUFFER_BYTES)) != NULL)
        vRingbufferReturnItem(playback, block);
    xEventGroupClearBits(state, FLUSH | PLAYING | PLAY_ENDED);
    xEventGroupSetBits(state, IDLE | FLUSHED);
}

/* Plays after the lead is buffered (or the Mac said the reply is complete). */
static void playback_task(void *unused)
{
    (void)unused;
    for (;;) {
        EventBits_t bits =
            xEventGroupWaitBits(state, PLAYING | FLUSH, pdFALSE, pdFALSE, portMAX_DELAY);
        if (bits & FLUSH) {
            flush_playback();
            continue;
        }
        UBaseType_t waiting = 0u;
        vRingbufferGetInfo(playback, NULL, NULL, NULL, NULL, &waiting);
        bool ended = (xEventGroupGetBits(state) & PLAY_ENDED) != 0;
        if (waiting < PLAY_LEAD_BYTES && !ended) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        size_t size = 0u;
        void *block = xRingbufferReceiveUpTo(playback, &size, pdMS_TO_TICKS(100),
                                             PLAY_BLOCK * sizeof(int16_t));
        if (block) {
            (void)jelli_agent_audio_write(block, size / sizeof(int16_t));
            atomic_fetch_add(&played_bytes, (unsigned)size);
            vRingbufferReturnItem(playback, block);
        } else if (ended) {
            xEventGroupClearBits(state, PLAYING);
            xEventGroupSetBits(state, IDLE);
        }
    }
}

bool jelli_agent_link_init(void)
{
    static StaticTask_t task_control;
    static StackType_t task_stack[4096u / sizeof(StackType_t)];
    state = xEventGroupCreateStatic(&state_storage);
    playback =
        xRingbufferCreateWithCaps(PLAY_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
    if (!state || !playback)
        return false;
    xEventGroupSetBits(state, IDLE);
    return xTaskCreateStatic(playback_task, "agent_play", sizeof(task_stack), NULL, 5u, task_stack,
                             &task_control) != NULL;
}

typedef struct {
    unsigned frames, read_failures, send_failures;
    int64_t send_us_total, send_us_max;
} Stream;

static void send_frames(esp_websocket_client_handle_t client, uint32_t seconds, Stream *stream)
{
    uint32_t total = seconds * 1000u / 60u;
    for (uint32_t sequence = 0u; sequence < total; ++sequence) {
        if (xEventGroupGetBits(state) & CLOSED)
            break;
        if (!jelli_agent_audio_read(capture, FRAME_SAMPLES)) {
            ++stream->read_failures;
            continue;
        }
        jelli_agent_frame_header(message, JELLI_AGENT_AUDIO_CHANNELS, sequence,
                                 (uint32_t)(esp_timer_get_time() / 1000));
        memcpy(message + JELLI_AGENT_FRAME_HEADER, capture, sizeof(capture));
        int64_t start = esp_timer_get_time();
        int sent = esp_websocket_client_send_bin(client, (const char *)message, sizeof(message),
                                                 pdMS_TO_TICKS(SEND_TIMEOUT_MS));
        int64_t took = esp_timer_get_time() - start;
        stream->send_us_total += took;
        stream->send_us_max = took > stream->send_us_max ? took : stream->send_us_max;
        if (sent != (int)sizeof(message))
            ++stream->send_failures;
        ++stream->frames;
    }
}

static void summarize(const Stream *stream, bool drained, char *out, size_t capacity)
{
    char body[256];
    (void)snprintf(body, sizeof(body),
                   "\"frames\":%u,\"read_failures\":%u,\"send_failures\":%u,"
                   "\"avg_send_us\":%" PRId64 ",\"max_send_us\":%" PRId64
                   ",\"received_bytes\":%u,\"dropped_bytes\":%u,\"played_bytes\":%u,"
                   "\"drained\":%s,\"internal_min\":%u",
                   stream->frames, stream->read_failures, stream->send_failures,
                   stream->frames ? stream->send_us_total / stream->frames : 0, stream->send_us_max,
                   atomic_load(&received_bytes), atomic_load(&dropped_bytes),
                   atomic_load(&played_bytes), drained ? "true" : "false",
                   (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    (void)snprintf(out, capacity, "{\"ok\":true,%s}", body);
}

/* Own transports, so a stalled read can be ended (abort_session) and a vanished
 * peer is noticed by TCP keepalive. The client does not free external transports. */
static bool make_transports(void)
{
    esp_transport_keep_alive_t keep_alive = {.keep_alive_enable = true,
                                             .keep_alive_idle = 5,
                                             .keep_alive_interval = 2,
                                             .keep_alive_count = 3};
    tcp_transport = esp_transport_tcp_init();
    ws_transport = tcp_transport ? esp_transport_ws_init(tcp_transport) : NULL;
    if (!ws_transport)
        return false;
    esp_transport_tcp_set_keep_alive(tcp_transport, &keep_alive);
    esp_transport_ws_set_path(ws_transport, "/agent");
    return true;
}

static void free_transports(void)
{
    if (ws_transport)
        (void)esp_transport_destroy(ws_transport);
    if (tcp_transport)
        (void)esp_transport_destroy(tcp_transport);
    ws_transport = tcp_transport = NULL;
}

/* Teardown that no peer can stall. esp_websocket_client_close and _destroy wait
 * without limit for the client's lock and task, which a peer that trickles a frame
 * or a handshake reply can hold for hours. A close frame is offered with a timeout,
 * then the socket is shut down so the task's read fails and destroy returns. */
static void stop_client(esp_websocket_client_handle_t client)
{
    if (xEventGroupGetBits(state) & CONNECTED)
        (void)esp_websocket_client_send_with_opcode(client, WS_TRANSPORT_OPCODES_CLOSE, NULL, 0,
                                                    pdMS_TO_TICKS(500));
    abort_session("teardown");
    (void)esp_websocket_client_destroy(client);
}

static esp_websocket_client_handle_t connect_to(const char *host, uint16_t port)
{
    if (!make_transports()) {
        free_transports();
        return NULL;
    }
    esp_websocket_client_config_t config = {.host = host,
                                            .port = port,
                                            .path = "/agent",
                                            .ext_transport = ws_transport,
                                            .buffer_size = 4096,
                                            .task_stack = 6144,
                                            .network_timeout_ms = CONNECT_TIMEOUT_MS,
                                            .disable_auto_reconnect = true};
    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    xEventGroupClearBits(state, CONNECTED | CLOSED);
    if (client &&
        (esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, on_event, NULL) != ESP_OK ||
         esp_websocket_client_start(client) != ESP_OK ||
         !(xEventGroupWaitBits(state, CONNECTED | CLOSED, pdFALSE, pdFALSE,
                               pdMS_TO_TICKS(CONNECT_TIMEOUT_MS)) &
           CONNECTED))) {
        stop_client(client);
        client = NULL;
    }
    if (!client)
        free_transports();
    return client;
}

void jelli_agent_link_stream(const char *host, uint16_t port, uint32_t seconds, char *out,
                             size_t capacity)
{
    if (!seconds || seconds > 30u || !jelli_agent_wifi_wait(5000u)) {
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"%s\"}",
                       seconds && seconds <= 30u ? "wifi_down" : "range");
        return;
    }
    /* A fresh session: nothing left over from the last one plays. */
    xEventGroupClearBits(state, FLUSHED);
    xEventGroupSetBits(state, FLUSH);
    if (!(xEventGroupWaitBits(state, FLUSHED, pdFALSE, pdTRUE, pdMS_TO_TICKS(500)) & FLUSHED)) {
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"playback_busy\"}");
        return;
    }
    atomic_store(&received_bytes, 0u);
    atomic_store(&dropped_bytes, 0u);
    atomic_store(&played_bytes, 0u);
    jelli_agent_wifi_power_save(false);
    esp_websocket_client_handle_t client = connect_to(host, port);
    if (!client) {
        jelli_agent_wifi_power_save(true);
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"connect\"}");
        return;
    }
    static const char hello[] = "{\"type\":\"hello\",\"app\":\"agent\",\"rate\":24000,"
                                "\"channels\":4,\"slots\":[\"MIC1\",\"MIC3\",\"MIC2\",\"MIC4\"],"
                                "\"frame_ms\":60}";
    _Static_assert(JELLI_AGENT_AUDIO_CHANNELS == 4u, "hello names four slots");
    (void)esp_websocket_client_send_text(client, hello, (int)strlen(hello), pdMS_TO_TICKS(1000));
    Stream stream = {0};
    send_frames(client, seconds, &stream);
    char end[128];
    int size =
        snprintf(end, sizeof(end), "{\"type\":\"stream_end\",\"frames\":%u,\"send_failures\":%u}",
                 stream.frames, stream.send_failures);
    (void)esp_websocket_client_send_text(client, end, size, pdMS_TO_TICKS(1000));
    /* A reply may still be playing: wait for it, bounded. */
    bool drained =
        (xEventGroupWaitBits(state, IDLE, pdFALSE, pdTRUE, pdMS_TO_TICKS(DRAIN_TIMEOUT_MS)) &
         IDLE) != 0;
    stop_client(client);
    free_transports();
    jelli_agent_wifi_power_save(true);
    summarize(&stream, drained, out, capacity);
    ESP_LOGI(TAG, "Stream done: %s", out);
}

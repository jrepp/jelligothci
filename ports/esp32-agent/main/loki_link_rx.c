#include "loki_link_rx.h"
#include "loki_models.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include <inttypes.h>
#include <string.h>

enum { MESSAGE_MAX = JELLI_LOKI_AUDIO_HEADER + JELLI_LOKI_AUDIO_MAX };
enum { TEXT = 0x1, BINARY = 0x2, CONTINUATION = 0x0 };

static const char *TAG = "loki_link";
/* WebSocket task only, set before it starts. */
static JelliAgentPlayerHooks player;
static void (*state_hook)(JelliLokiState state);
static uint8_t inbound[MESSAGE_MAX];
static size_t inbound_size;
static uint8_t inbound_opcode;
static bool assembling, welcomed;
static uint32_t rx_seq;
static JelliLokiMessage decoded;

void jelli_agent_link_on_state(void (*hook)(JelliLokiState state)) { state_hook = hook; }

void jelli_agent_link_rx_hooks(const JelliAgentPlayerHooks *hooks)
{
    if (hooks)
        player = *hooks;
}

void jelli_agent_link_rx_reset(void)
{
    inbound_size = 0u;
    assembling = welcomed = false;
    rx_seq = 0u;
    jelli_agent_models_forget();
}

/* Audio control stays inside the link, and the list of models goes to Settings
 * (ADR-018), which shows only the latest; the rest goes to the app's inbox. */
static void route(void)
{
    switch (decoded.type) {
    case JELLI_LOKI_UNKNOWN:
        break;
    case JELLI_LOKI_WELCOME:
        jelli_agent_link_abort("welcome repeated");
        break;
    case JELLI_LOKI_REPLY_BEGIN:
        if (player.reply_begin)
            player.reply_begin(decoded.reply);
        break;
    case JELLI_LOKI_REPLY_END:
        if (player.reply_end)
            player.reply_end(decoded.reply);
        break;
    case JELLI_LOKI_STOP_AUDIO:
        if (player.stop_audio)
            player.stop_audio();
        break;
    case JELLI_LOKI_MODELS:
        jelli_agent_models_offer(&decoded);
        ESP_LOGI(TAG, "The bridge offers %u models; the default is %s", decoded.model_count,
                 decoded.models[decoded.current]);
        break;
    case JELLI_LOKI_STATE:
        if (state_hook)
            state_hook(decoded.state);
        (void)jelli_agent_link_deliver(&decoded);
        break;
    case JELLI_LOKI_CAPTION:
    case JELLI_LOKI_ASK:
    case JELLI_LOKI_ASK_CANCEL:
    case JELLI_LOKI_SETTINGS:
        (void)jelli_agent_link_deliver(&decoded);
        break;
    }
}

static void dispatch_text(void)
{
    JelliLokiResult result = jelli_loki_decode((const char *)inbound, inbound_size, &decoded);
    if (result == JELLI_LOKI_INVALID) {
        jelli_agent_link_abort("invalid message");
        return;
    }
    if (rx_seq == UINT32_MAX || decoded.seq != rx_seq + 1u) {
        jelli_agent_link_abort("sequence");
        return;
    }
    rx_seq = decoded.seq;
    if (!welcomed) {
        if (decoded.type != JELLI_LOKI_WELCOME) {
            jelli_agent_link_abort("welcome must come first");
            return;
        }
        welcomed = true;
        ESP_LOGI(TAG, "Welcomed by %s (voice %s)", decoded.bridge, decoded.voice);
        jelli_agent_link_set(JELLI_LINK_WELCOMED);
        return;
    }
    route();
}

static void dispatch_binary(void)
{
    JelliLokiAudio audio;
    size_t size = 0u;
    if (!welcomed ||
        !jelli_loki_audio_parse(inbound, inbound_size, JELLI_LOKI_REPLY, 1u, &audio, &size)) {
        jelli_agent_link_abort("invalid audio");
        return;
    }
    if (player.reply_audio)
        player.reply_audio(audio.stream, inbound + JELLI_LOKI_AUDIO_HEADER, size);
}

/* Whether this event starts, continues, or (wrongly) interleaves a message. */
static bool begin(const esp_websocket_event_data_t *event)
{
    if (event->payload_offset != 0)
        return true; /* More of the current frame. */
    if (event->op_code == TEXT || event->op_code == BINARY) {
        if (assembling) {
            jelli_agent_link_abort("interleaved message");
            return false;
        }
        assembling = true;
        inbound_opcode = event->op_code;
        inbound_size = 0u;
        return true;
    }
    if (!assembling)
        jelli_agent_link_abort("stray continuation");
    return assembling;
}

/* Network reads and fragmented messages arrive in parts; a message is handled whole. */
static void on_data(const esp_websocket_event_data_t *event)
{
    if (event->op_code != TEXT && event->op_code != BINARY && event->op_code != CONTINUATION)
        return; /* Control frames: the client answers pings and tracks pongs. */
    if (event->payload_len < 0 || event->data_len < 0 || (size_t)event->payload_len > MESSAGE_MAX) {
        jelli_agent_link_abort("oversized message");
        return;
    }
    if (!begin(event))
        return;
    if (event->data_len == 0 && event->payload_offset < event->payload_len) {
        jelli_agent_link_abort("message stalled"); /* A read timed out inside a frame. */
        return;
    }
    if ((size_t)event->data_len > sizeof(inbound) - inbound_size) {
        jelli_agent_link_abort("oversized message");
        return;
    }
    memcpy(inbound + inbound_size, event->data_ptr, (size_t)event->data_len);
    inbound_size += (size_t)event->data_len;
    if (event->payload_offset + event->data_len < event->payload_len || !event->fin)
        return; /* More of this frame, or more frames. */
    assembling = false;
    if (inbound_opcode == TEXT)
        dispatch_text();
    else
        dispatch_binary();
}

/* esp_event_handler_t fixes this signature (loki_link.c registers it), so data
 * cannot be a const pointer. */
// cppcheck-suppress constParameterPointer
void jelli_agent_link_on_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    (void)base;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        jelli_agent_link_set(JELLI_LINK_CONNECTED);
        jelli_agent_link_send_hello();
        break;
    case WEBSOCKET_EVENT_DATA:
        on_data(data);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
    case WEBSOCKET_EVENT_FINISH:
        /* These can arrive inside another task's failed send, which holds the send
         * lock: offline first, and nothing here sends (memo-027). */
        jelli_agent_link_closed("disconnected");
        if (player.session_ended)
            player.session_ended(); /* A reply cut off with its session is stale. */
        if (id == WEBSOCKET_EVENT_FINISH)
            jelli_agent_link_set(JELLI_LINK_FINISHED);
        break;
    default:
        break;
    }
}

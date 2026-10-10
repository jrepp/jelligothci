#include "jelli/agent_loki.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* What the board sends, written exactly as docs/loki-protocol.md orders it, so the
 * bridge's tests can decode the same bytes from tests/vectors/loki-1.tsv. */

typedef struct {
    char *out;
    size_t capacity, used;
    bool failed;
} Json;

static void put(Json *json, const char *text)
{
    size_t length = strlen(text);
    if (json->failed || length >= json->capacity - json->used) {
        json->failed = true;
        return;
    }
    memcpy(json->out + json->used, text, length + 1u);
    json->used += length;
}

static void put_u32(Json *json, const char *key, uint32_t value)
{
    char text[48];
    (void)snprintf(text, sizeof(text), ",\"%s\":%" PRIu32, key, value);
    put(json, text);
}

static void put_int(Json *json, const char *key, int value)
{
    char text[48];
    (void)snprintf(text, sizeof(text), ",\"%s\":%d", key, value);
    put(json, text);
}

static void put_bool(Json *json, const char *key, bool value)
{
    char text[48];
    (void)snprintf(text, sizeof(text), ",\"%s\":%s", key, value ? "true" : "false");
    put(json, text);
}

/* Strings the board sends are printable ASCII without quotes or backslashes (checked
 * by the callers), so they need no escaping. */
static void put_string(Json *json, const char *key, const char *value)
{
    put(json, ",\"");
    put(json, key);
    put(json, "\":\"");
    put(json, value);
    put(json, "\"");
}

static bool printable(const char *text, size_t max_bytes)
{
    size_t length = text ? strlen(text) : 0u;
    if (!text || length > max_bytes)
        return false;
    for (size_t i = 0u; i < length; ++i)
        if (text[i] < 0x20 || text[i] > 0x7E || text[i] == '"' || text[i] == '\\')
            return false;
    return true;
}

static bool identifier(const char *text)
{
    size_t length = text ? strlen(text) : 0u;
    if (length < 1u || length > JELLI_LOKI_ID - 1u)
        return false;
    for (size_t i = 0u; i < length; ++i) {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return false;
    }
    return true;
}

static bool lower_hex(const char *text, size_t length)
{
    if (!text || strlen(text) != length)
        return false;
    for (size_t i = 0u; i < length; ++i)
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f')))
            return false;
    return true;
}

static bool hello(Json *json, const JelliLokiOut *message)
{
    if (!printable(message->fw, 40u) || !printable(message->board, 16u) || message->channels < 1u ||
        message->channels > 4u || message->frame_ms < 20u || message->frame_ms > 100u)
        return false;
    put_u32(json, "proto", 1u);
    put_string(json, "app", "loki-agent");
    put_string(json, "fw", message->fw);
    put_string(json, "board", message->board);
    put_u32(json, "rate", JELLI_LOKI_RATE);
    put_u32(json, "channels", message->channels);
    put_u32(json, "frame_ms", message->frame_ms);
    return true;
}

static bool answer(Json *json, const JelliLokiOut *message)
{
    if (!identifier(message->id) || !lower_hex(message->nonce, JELLI_LOKI_NONCE - 1u) ||
        !lower_hex(message->digest, JELLI_LOKI_DIGEST - 1u) || message->choice > 3u)
        return false;
    put_string(json, "id", message->id);
    put_string(json, "nonce", message->nonce);
    put_string(json, "digest", message->digest);
    put_u32(json, "choice", message->choice);
    return true;
}

static bool fields(Json *json, const JelliLokiOut *message)
{
    switch (message->type) {
    case JELLI_LOKI_HELLO:
        return hello(json, message);
    case JELLI_LOKI_PTT_START:
        put_u32(json, "stream", message->stream);
        return message->stream != 0u;
    case JELLI_LOKI_PTT_END:
        put_u32(json, "stream", message->stream);
        put_u32(json, "frames", message->frames);
        put_u32(json, "dropped", message->dropped);
        return message->stream != 0u;
    case JELLI_LOKI_CANCEL:
        return true;
    case JELLI_LOKI_ANSWER:
        return answer(json, message);
    case JELLI_LOKI_PLAYED:
        put_u32(json, "reply", message->reply);
        put_bool(json, "complete", message->complete);
        return message->reply != 0u;
    case JELLI_LOKI_STATUS:
        put_int(json, "battery", message->battery);
        put_bool(json, "charging", message->charging);
        put_int(json, "rssi", message->rssi);
        return message->battery >= -1 && message->battery <= 100 && message->rssi >= -127 &&
               message->rssi <= 0;
    case JELLI_LOKI_SET_MODEL: /* Its charset needs no escaping either. */
        if (!message->model || !jelli_loki_model_name(message->model, strlen(message->model)))
            return false;
        put_string(json, "model", message->model);
        return true;
    case JELLI_LOKI_NONE:
        return false;
    }
    return false;
}

size_t jelli_loki_encode(const JelliLokiOut *message, char *out, size_t capacity)
{
    static const char *const NAMES[] = {"",       "hello",  "ptt_start", "ptt_end",  "cancel",
                                        "answer", "played", "status",    "set_model"};
    _Static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == JELLI_LOKI_SET_MODEL + 1,
                   "a name for every type the board sends");
    Json json = {out, capacity, 0u, !out || capacity == 0u};
    char head[48];
    if (out && capacity)
        out[0] = '\0'; /* Nothing partial can be sent. */
    if (!message || message->seq == 0u || (unsigned)message->type == 0u ||
        (unsigned)message->type >= sizeof(NAMES) / sizeof(NAMES[0]))
        return 0u;
    (void)snprintf(head, sizeof(head), "{\"t\":\"%s\",\"seq\":%" PRIu32, NAMES[message->type],
                   message->seq);
    put(&json, head);
    bool valid = fields(&json, message);
    put(&json, "}");
    if (!valid || json.failed || json.used > JELLI_LOKI_MAX_OUT) {
        if (out && capacity)
            out[0] = '\0';
        return 0u;
    }
    return json.used;
}

static void put_le32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0u; i < 4u; ++i)
        out[i] = (uint8_t)(value >> (8u * i));
}

static uint32_t get_le32(const uint8_t *in)
{
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

void jelli_loki_audio_header(const JelliLokiAudio *audio, uint8_t out[JELLI_LOKI_AUDIO_HEADER])
{
    out[0] = 'L';
    out[1] = 'K';
    out[2] = (uint8_t)audio->kind;
    out[3] = (uint8_t)(audio->final ? 1u : 0u);
    put_le32(out + 4, audio->stream);
    put_le32(out + 8, audio->seq);
}

bool jelli_loki_audio_parse(const uint8_t *frame, size_t size, JelliLokiAudioKind expected,
                            unsigned channels, JelliLokiAudio *audio, size_t *payload_size)
{
    if (!frame || !audio || !payload_size || channels < 1u || channels > 4u ||
        size <= JELLI_LOKI_AUDIO_HEADER || size > JELLI_LOKI_AUDIO_HEADER + JELLI_LOKI_AUDIO_MAX)
        return false;
    size_t payload = size - JELLI_LOKI_AUDIO_HEADER;
    uint32_t stream = get_le32(frame + 4);
    if (frame[0] != 'L' || frame[1] != 'K' || frame[2] != (uint8_t)expected || frame[3] > 1u ||
        stream == 0u || payload % ((size_t)2u * channels) != 0u)
        return false;
    audio->kind = expected;
    audio->final = frame[3] == 1u;
    audio->stream = stream;
    audio->seq = get_le32(frame + 8);
    *payload_size = payload;
    return true;
}

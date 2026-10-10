/* loki/1 at its limits (docs/loki-protocol.md): the board's encoder at every field's
 * bounds and one past, every output capacity, the canonical writer's capacity and
 * bounds, what a decoded message leaves zero, audio frame sizes, and SHA-256 around
 * each padding boundary against digests from Python's hashlib. */
#include "jelli/agent_loki.h"
#include "jelli/agent_sha256.h"
#include "loki_vectors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

enum { CANARY = 0x5A, ROOM = JELLI_LOKI_MAX_OUT + 16 };

static const char NONCE[] = "00112233445566778899aabbccddeeff";
static const char DIGEST[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char F40[] = "~!#$%&'()*+,-./0123456789:;<=>?@ABCDEFGH"; /* Printable, 40 bytes. */

/* Encodes at every capacity from 0 to one past the length: each short one returns 0,
 * leaves an empty string, and writes nothing past its capacity. */
static size_t encode_everywhere(const JelliLokiOut *message, char *json)
{
    static char out[ROOM];
    size_t size = jelli_loki_encode(message, json, ROOM);
    if (!size)
        return 0u;
    CHECK(size == strlen(json) && size <= JELLI_LOKI_MAX_OUT);
    for (size_t capacity = 0u; capacity <= size + 1u; ++capacity) {
        memset(out, CANARY, sizeof(out));
        size_t result = jelli_loki_encode(message, out, capacity);
        CHECK(result == (capacity > size ? size : 0u));
        CHECK(capacity == 0u || (result ? !strcmp(out, json) : out[0] == '\0'));
        for (size_t i = capacity; i < sizeof(out); ++i)
            CHECK(out[i] == (char)CANARY);
    }
    return size;
}

static void expect_json(const JelliLokiOut *message, const char *expected)
{
    char json[ROOM];
    size_t size = encode_everywhere(message, json);
    if (!expected) {
        CHECK(size == 0u);
        return;
    }
    if (strcmp(json, expected) != 0)
        fprintf(stderr, "got  %s\nwant %s\n", json, expected);
    CHECK(size > 0u && !strcmp(json, expected));
}

static void hello_limits(void)
{
    JelliLokiOut m = {.type = JELLI_LOKI_HELLO, .seq = UINT32_MAX, .fw = F40, .board = F40 + 24};
    m.channels = 4u;
    m.frame_ms = 100u;
    expect_json(&m, "{\"t\":\"hello\",\"seq\":4294967295,\"proto\":1,\"app\":\"loki-agent\",\"fw\":"
                    "\"~!#$%&'()*+,-./0123456789:;<=>?@ABCDEFGH\",\"board\":\"9:;<=>?@ABCDEFGH\","
                    "\"rate\":24000,\"channels\":4,\"frame_ms\":100}");
    m = (JelliLokiOut){.type = JELLI_LOKI_HELLO, .seq = 1u, .fw = "", .board = ""};
    m.channels = 1u;
    m.frame_ms = 20u;
    expect_json(&m, "{\"t\":\"hello\",\"seq\":1,\"proto\":1,\"app\":\"loki-agent\",\"fw\":\"\","
                    "\"board\":\"\",\"rate\":24000,\"channels\":1,\"frame_ms\":20}");
    static const char *const BAD_TEXT[] = {"a\x7f", "a\x1f", "\xc3\xa9", "a\tb", NULL};
    for (size_t i = 0u; i < COUNT(BAD_TEXT); ++i) {
        m.fw = BAD_TEXT[i];
        expect_json(&m, NULL);
        m.fw = "";
        m.board = BAD_TEXT[i];
        expect_json(&m, NULL);
        m.board = "";
    }
    m.board = F40 + 23; /* 17 bytes */
    expect_json(&m, NULL);
}

static void answer_limits(void)
{
    char id32[33], id33[34];
    memset(id32, 'Z', 32u);
    id32[32] = '\0';
    memset(id33, '_', 33u);
    id33[33] = '\0';
    JelliLokiOut m = {
        .type = JELLI_LOKI_ANSWER, .seq = 2u, .id = id32, .nonce = NONCE, .digest = DIGEST};
    m.choice = 3u;
    char expected[ROOM];
    (void)snprintf(expected, sizeof(expected),
                   "{\"t\":\"answer\",\"seq\":2,\"id\":\"%s\",\"nonce\":\"%s\",\"digest\":\"%s\","
                   "\"choice\":3}",
                   id32, NONCE, DIGEST);
    expect_json(&m, expected);
    m.id = "-";
    m.choice = 0u;
    CHECK(encode_everywhere(&m, expected) > 0u);
    const char *const BAD_IDS[] = {id33, "a.b", "\xc3\xa9", "a\"", NULL};
    for (size_t i = 0u; i < COUNT(BAD_IDS); ++i) {
        m.id = BAD_IDS[i];
        expect_json(&m, NULL);
    }
    m.id = "a";
    static const char *const BAD_NONCES[] = {"0011223344556677889aabbccddeeff",
                                             "00112233445566778899aabbccddeeff0",
                                             "00112233445566778899aabbccddeefg", NULL};
    for (size_t i = 0u; i < COUNT(BAD_NONCES); ++i) {
        m.nonce = BAD_NONCES[i];
        expect_json(&m, NULL);
        m.nonce = NONCE;
        m.digest = BAD_NONCES[i];
        expect_json(&m, NULL);
        m.digest = DIGEST;
    }
    m.choice = 4u;
    expect_json(&m, NULL);
}

static void number_limits(void)
{
    const struct {
        JelliLokiOut message;
        const char *json;
    } cases[] = {
        {{.type = JELLI_LOKI_PTT_START, .seq = 1u, .stream = UINT32_MAX},
         "{\"t\":\"ptt_start\",\"seq\":1,\"stream\":4294967295}"},
        {{.type = JELLI_LOKI_PTT_END,
          .seq = 1u,
          .stream = 1u,
          .frames = UINT32_MAX,
          .dropped = UINT32_MAX},
         "{\"t\":\"ptt_end\",\"seq\":1,\"stream\":1,\"frames\":4294967295,\"dropped\":4294967295}"},
        {{.type = JELLI_LOKI_PTT_END, .seq = 1u, .stream = 1u},
         "{\"t\":\"ptt_end\",\"seq\":1,\"stream\":1,\"frames\":0,\"dropped\":0}"},
        {{.type = JELLI_LOKI_PLAYED, .seq = 1u, .reply = UINT32_MAX},
         "{\"t\":\"played\",\"seq\":1,\"reply\":4294967295,\"complete\":false}"},
        {{.type = JELLI_LOKI_STATUS, .seq = 1u, .battery = 100, .rssi = 0, .charging = true},
         "{\"t\":\"status\",\"seq\":1,\"battery\":100,\"charging\":true,\"rssi\":0}"},
        {{.type = JELLI_LOKI_STATUS, .seq = 1u, .battery = -1, .rssi = -127},
         "{\"t\":\"status\",\"seq\":1,\"battery\":-1,\"charging\":false,\"rssi\":-127}"},
        {{.type = JELLI_LOKI_CANCEL, .seq = UINT32_MAX}, "{\"t\":\"cancel\",\"seq\":4294967295}"},
        {{.type = JELLI_LOKI_STATUS, .seq = 1u, .battery = 101}, NULL},
        {{.type = JELLI_LOKI_STATUS, .seq = 1u, .battery = -2}, NULL},
        {{.type = JELLI_LOKI_STATUS, .seq = 1u, .rssi = 1}, NULL},
        {{.type = JELLI_LOKI_STATUS, .seq = 1u, .rssi = -128}, NULL},
        {{.type = JELLI_LOKI_PTT_START, .seq = 1u}, NULL},
        {{.type = JELLI_LOKI_PLAYED, .seq = 1u, .complete = true}, NULL},
        {{.type = JELLI_LOKI_CANCEL}, NULL},
        /* Out of range on purpose: the encoder bounds the type a caller sets. */
        /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
        {{.type = (JelliLokiOutType)(JELLI_LOKI_SET_MODEL + 1), .seq = 1u}, NULL},
        /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
        {{.type = (JelliLokiOutType)99, .seq = 1u}, NULL},
        {{.type = JELLI_LOKI_NONE, .seq = 1u}, NULL},
    };
    for (size_t i = 0u; i < COUNT(cases); ++i)
        expect_json(&cases[i].message, cases[i].json);
    JelliLokiOut hello = {.type = JELLI_LOKI_HELLO, .seq = 1u, .fw = "1", .board = "b"};
    static const unsigned CHANNELS[][2] = {{0u, 0u}, {1u, 1u}, {4u, 1u}, {5u, 0u}};
    static const unsigned FRAMES[][2] = {{19u, 0u}, {20u, 1u}, {100u, 1u}, {101u, 0u}};
    for (size_t i = 0u; i < COUNT(CHANNELS); ++i) {
        hello.channels = CHANNELS[i][0];
        hello.frame_ms = 60u;
        char json[ROOM];
        CHECK((encode_everywhere(&hello, json) > 0u) == (CHANNELS[i][1] == 1u));
        hello.channels = 2u;
        hello.frame_ms = FRAMES[i][0];
        CHECK((encode_everywhere(&hello, json) > 0u) == (FRAMES[i][1] == 1u));
    }
}

/* The canonical writer: every capacity, and bounds on messages built by hand. */
static void canonical_limits(void)
{
    static JelliLokiMessage message;
    static char full[8192], out[8192];
    uint8_t scratch[4096];
    size_t input_size;
    const LokiVector *vector = loki_vector_named("size-2048-ask-max");
    const char *input = loki_control_bytes(vector, scratch, sizeof(scratch), &input_size);
    CHECK(jelli_loki_decode(input, input_size, &message) == JELLI_LOKI_OK);
    size_t size = jelli_loki_canonical(&message, full, sizeof(full));
    CHECK(size > 2048u && !strcmp(full, vector->canonical));
    for (size_t capacity = 0u; capacity <= size + 1u; ++capacity) {
        memset(out, CANARY, sizeof(out));
        CHECK(jelli_loki_canonical(&message, out, capacity) == (capacity > size ? size : 0u));
        for (size_t i = capacity; i < sizeof(out); ++i)
            CHECK(out[i] == (char)CANARY);
    }
    /* The digest hashes the canonical form from kind= to the options. */
    char digest[JELLI_LOKI_DIGEST], expected[65];
    const char *from = strstr(full, "kind="), *to = strstr(full, " timeout_ms=");
    uint8_t hash[JELLI_SHA256_SIZE];
    JelliSha256 sha;
    jelli_sha256_init(&sha);
    jelli_sha256_update(&sha, from, (size_t)(to - from));
    jelli_sha256_final(&sha, hash);
    for (size_t i = 0u; i < sizeof(hash); ++i)
        (void)snprintf(expected + 2u * i, 3u, "%02x", (unsigned)hash[i]);
    jelli_loki_card_digest(&message, digest);
    CHECK(!strcmp(digest, expected));
    /* Values a caller sets out of range are written as "?" or cut to four options. */
    message.kind = (JelliLokiKind)7;
    message.option_count = 9u;
    CHECK(jelli_loki_canonical(&message, out, sizeof(out)) > 0u && strstr(out, " kind=? "));
    CHECK(strstr(out, "options=[\"\\xF0") && strstr(out, "\"] timeout_ms=600000"));
    unsigned commas = 0u; /* Only between options: no other field of this card has one. */
    for (const char *at = out; *at; ++at)
        if (*at == ',')
            ++commas;
    CHECK(commas == JELLI_LOKI_OPTIONS - 1u);
    memset(&message, 0, sizeof(message));
    message.type = JELLI_LOKI_STATE;
    message.state = (JelliLokiState)42;
    CHECK(jelli_loki_canonical(&message, out, sizeof(out)) > 0u &&
          !strcmp(out, "state seq=0 state=? caption=\"\" progress=0 queue=0"));
}

static bool zero(const void *bytes, size_t size)
{
    const unsigned char *at = bytes;
    for (size_t i = 0u; i < size; ++i)
        if (at[i])
            return false;
    return true;
}

/* The strings and options of a message are all zero bytes. */
static bool no_text(const JelliLokiMessage *m)
{
    return zero(m->bridge, sizeof(m->bridge)) && zero(m->voice, sizeof(m->voice)) &&
           zero(m->text, sizeof(m->text)) && zero(m->id, sizeof(m->id)) &&
           zero(m->nonce, sizeof(m->nonce)) && zero(m->title, sizeof(m->title)) &&
           zero(m->detail, sizeof(m->detail)) && zero(m->options, sizeof(m->options)) &&
           zero(m->models, sizeof(m->models));
}

/* Only type and seq set, progress at its default of -1, and everything else zero. */
static bool clean(const JelliLokiMessage *m, JelliLokiType type, uint32_t seq)
{
    return m->type == type && m->seq == seq && m->progress == -1 && !m->proto && !m->state &&
           !m->queue && !m->final && !m->kind && !m->option_count && !m->timeout_ms && !m->reply &&
           !m->rate && !m->frames && !m->volume && !m->model_count && !m->current && no_text(m);
}

/* A decode starts from a clean message: nothing of an earlier one survives, an
 * ignored message sets only type and seq, and progress defaults to -1. */
static void decode_resets(void)
{
    static JelliLokiMessage message;
    static const char ASK[] = "{\"t\":\"ask\",\"seq\":3,\"id\":\"a\",\"nonce\":"
                              "\"00112233445566778899aabbccddeeff\",\"kind\":\"choose\",\"title\":"
                              "\"T\",\"detail\":\"D\",\"options\":[\"a\",\"b\",\"c\"],"
                              "\"timeout_ms\":1000}";
    static const char UNKNOWN[] =
        "{\"t\":\"confetti\",\"seq\":9,\"volume\":5,\"state\":\"failure\"}";
    static const char STOP[] = "{\"t\":\"stop_audio\",\"seq\":4}";
    CHECK(jelli_loki_decode(ASK, strlen(ASK), &message) == JELLI_LOKI_OK);
    CHECK(message.option_count == 3u && !strcmp(message.options[2], "c"));
    CHECK(jelli_loki_decode(UNKNOWN, strlen(UNKNOWN), &message) == JELLI_LOKI_IGNORED);
    CHECK(clean(&message, JELLI_LOKI_UNKNOWN, 9u));
    CHECK(jelli_loki_decode(ASK, strlen(ASK), &message) == JELLI_LOKI_OK);
    CHECK(jelli_loki_decode(STOP, strlen(STOP), &message) == JELLI_LOKI_OK);
    CHECK(clean(&message, JELLI_LOKI_STOP_AUDIO, 4u));
    static const char MODELS[] =
        "{\"t\":\"models\",\"seq\":5,\"models\":[\"a\",\"b\"],\"current\":1}";
    CHECK(jelli_loki_decode(MODELS, strlen(MODELS), &message) == JELLI_LOKI_OK);
    CHECK(message.model_count == 2u && message.current == 1u && !strcmp(message.models[1], "b"));
    CHECK(jelli_loki_decode(STOP, strlen(STOP), &message) == JELLI_LOKI_OK);
    CHECK(clean(&message, JELLI_LOKI_STOP_AUDIO, 4u));
}

static void audio_limits(void)
{
    static uint8_t frame[JELLI_LOKI_AUDIO_HEADER + JELLI_LOKI_AUDIO_MAX + 2u];
    JelliLokiAudio audio = {JELLI_LOKI_REPLY, true, UINT32_MAX, UINT32_MAX}, parsed;
    size_t payload;
    memset(frame, 0, sizeof(frame));
    jelli_loki_audio_header(&audio, frame);
    static const uint8_t HEADER[] = {'L',  'K',  2,    1,    0xff, 0xff,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    CHECK(!memcmp(frame, HEADER, sizeof(HEADER)));
    static const struct {
        size_t payload;
        unsigned channels;
        bool ok;
    } cases[] = {{0u, 1u, false},   {1u, 1u, false},    {2u, 1u, true},    {2u, 2u, false},
                 {4u, 2u, true},    {6u, 3u, true},     {4u, 3u, false},   {8u, 4u, true},
                 {4800u, 1u, true}, {4800u, 3u, true},  {4800u, 4u, true}, {4802u, 1u, false},
                 {4798u, 1u, true}, {4798u, 2u, false}, {12u, 0u, false},  {12u, 5u, false}};
    for (size_t i = 0u; i < COUNT(cases); ++i) {
        size_t size = JELLI_LOKI_AUDIO_HEADER + cases[i].payload;
        bool ok = jelli_loki_audio_parse(frame, size, JELLI_LOKI_REPLY, cases[i].channels, &parsed,
                                         &payload);
        CHECK(ok == cases[i].ok);
        CHECK(!ok || (payload == cases[i].payload && parsed.final && parsed.stream == UINT32_MAX &&
                      parsed.seq == UINT32_MAX && parsed.kind == JELLI_LOKI_REPLY));
    }
    CHECK(!jelli_loki_audio_parse(frame, 14u, JELLI_LOKI_MIC, 1u, &parsed, &payload));
    CHECK(!jelli_loki_audio_parse(frame, 14u, JELLI_LOKI_REPLY, 1u, NULL, &payload));
    CHECK(!jelli_loki_audio_parse(frame, 14u, JELLI_LOKI_REPLY, 1u, &parsed, NULL));
    frame[3] = 3u; /* The last-frame bit with another set. */
    CHECK(!jelli_loki_audio_parse(frame, 14u, JELLI_LOKI_REPLY, 1u, &parsed, &payload));
}

/* Digests from Python's hashlib of bytes (31 * i + 7) mod 256. */
static void sha256_padding_boundaries(void)
{
    static const struct {
        size_t size;
        const char *hex;
    } cases[] = {
        {1u, "ca358758f6d27e6cf45272937977a748fd88391db679ceda7dc7bf1f005ee879"},
        {55u, "8aa994584139d128848eeebc4e815639ba5ab6e6e39574195a63ac4f14f7c43b"},
        {56u, "ad574708f75c044c9b85de64cb568ee7711ff4f36448c6242f053ba8f6cc2b63"},
        {57u, "5b46e502092be01b1100193e089fdda95638c12e19a1d24f308eb2c3d3ae849d"},
        {63u, "280ed3e8ff1df845b2e7dfe6ac6cee817bef20e783cc65abc41b818b4d2fe076"},
        {64u, "c6ab9724ade5b6a7a1edfffb12f3aa9181351355af8fd08c919952ad211339dd"},
        {65u, "788367c73c7ddf4c53f65e68cc0d943e6227ab55b0e78ba63ace822b1c6301c0"},
        {111u, "dd1413178fb627f9abbc041ffe39c44aa7aaa0e2e6d2ca5c4528ac7073a2da45"},
        {112u, "a65c92dac124062d0ab951a42773cb04fc98d1d4bf8897b176f8cff3509d379e"},
        {119u, "3d610547d68216dedf7435a4fb6260353911f6b3fd3f18805ddb8be285d726fe"},
        {120u, "1f80156a804cb7862ad113e8200e9d74499723e7c7854d5f48776d3148e09656"},
        {121u, "614571410beab3df68d50132a341d338575653da8374c630441bbe380b9b3136"},
        {127u, "192409cd280e14b743642ad1343fbd3e82d9305de72c078117745a679210cc3d"},
        {128u, "cc548ca2dec1f6fe4f58b2e27aa9c7521607df1130d140b55a4dad0665302356"},
        {129u, "81e89a7b2911aaa7795f9e3d4910cb47d6cd2b00d83b8399481527261a1a7519"},
        {1000u, "5097e7d587352f5097062ae679f37bda5802d9f875aba14c8cb4d1a188ada179"},
    };
    static uint8_t data[1000];
    for (size_t i = 0u; i < sizeof(data); ++i)
        data[i] = (uint8_t)(31u * i + 7u);
    for (size_t c = 0u; c < COUNT(cases); ++c) {
        /* Whole, then split at every point (each split also hashing an empty piece). */
        for (size_t split = 0u; split <= cases[c].size; split += cases[c].size > 200u ? 61u : 1u) {
            JelliSha256 sha;
            uint8_t digest[JELLI_SHA256_SIZE];
            char hex[65];
            jelli_sha256_init(&sha);
            jelli_sha256_update(&sha, data, split);
            jelli_sha256_update(&sha, data + split, 0u);
            jelli_sha256_update(&sha, data + split, cases[c].size - split);
            jelli_sha256_final(&sha, digest);
            for (size_t i = 0u; i < sizeof(digest); ++i)
                (void)snprintf(hex + 2u * i, 3u, "%02x", (unsigned)digest[i]);
            CHECK(!strcmp(hex, cases[c].hex));
        }
    }
}

int main(void)
{
    loki_vectors_load();
    hello_limits();
    answer_limits();
    number_limits();
    canonical_limits();
    decode_resets();
    audio_limits();
    sha256_padding_boundaries();
    printf("Loki limit checks passed\n");
    return 0;
}

#include "jelli/agent_loki.h"
#include "jelli/agent_sha256.h"
#include "loki_vectors.h"
#include <inttypes.h>
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

/* Scratch for hex inputs. */
static uint8_t bytes[16 * 1024];
static unsigned decoded, audio_frames;

typedef LokiVector Vector;

static void check_audio(const Vector *v)
{
    JelliLokiAudio audio;
    size_t payload = 0u, size = loki_unhex(v->input + 4, bytes, sizeof(bytes));
    bool ok = jelli_loki_audio_parse(bytes, size, JELLI_LOKI_REPLY, 1u, &audio, &payload);
    if (strcmp(v->verdict, "ok") != 0) {
        CHECK(!ok);
        return;
    }
    CHECK(ok);
    char canonical[128];
    (void)snprintf(canonical, sizeof(canonical),
                   "audio kind=reply final=%s stream=%" PRIu32 " seq=%" PRIu32 " bytes=%zu",
                   audio.final ? "true" : "false", audio.stream, audio.seq, payload);
    CHECK(!strcmp(canonical, v->canonical));
    ++audio_frames;
}

static void check_control(const Vector *v)
{
    static JelliLokiMessage message;
    size_t size;
    const char *text = loki_control_bytes(v, bytes, sizeof(bytes), &size);
    JelliLokiResult result = jelli_loki_decode(text, size, &message);
    if (!strcmp(v->verdict, "reject")) {
        if (result != JELLI_LOKI_INVALID)
            fprintf(stderr, "vector %s was not rejected\n", v->name);
        CHECK(result == JELLI_LOKI_INVALID);
    } else if (!strcmp(v->verdict, "ignore")) {
        CHECK(result == JELLI_LOKI_IGNORED && message.type == JELLI_LOKI_UNKNOWN && message.seq);
    } else {
        if (result != JELLI_LOKI_OK)
            fprintf(stderr, "vector %s did not decode\n", v->name);
        CHECK(result == JELLI_LOKI_OK);
        static char canonical[8192];
        CHECK(jelli_loki_canonical(&message, canonical, sizeof(canonical)) > 0u);
        if (strcmp(canonical, v->canonical) != 0)
            fprintf(stderr, "vector %s:\n  got  %s\n  want %s\n", v->name, canonical, v->canonical);
        CHECK(!strcmp(canonical, v->canonical));
        ++decoded;
    }
}

/* Every bridge-to-board vector decodes as the file says. */
static void decodes_the_vectors(void)
{
    for (unsigned i = 0u; i < loki_vector_count(); ++i) {
        const Vector *v = loki_vector(i);
        if (strcmp(v->direction, "b2d") != 0)
            continue;
        if (!strncmp(v->input, "hex:", 4u))
            check_audio(v);
        else
            check_control(v);
    }
    CHECK(decoded > 25u && audio_frames == 2u);
}

static const Vector *find(const char *name) { return loki_vector_named(name); }

/* An encoded message must appear verbatim as a d2b "ok" input, which the bridge's
 * tests decode. */
static void check_sent(const char *text)
{
    for (unsigned i = 0u; i < loki_vector_count(); ++i) {
        const Vector *v = loki_vector(i);
        if (!strcmp(v->direction, "d2b") && !strcmp(v->verdict, "ok") && !strcmp(v->input, text))
            return;
    }
    fprintf(stderr, "encoded message is not a d2b vector: %s\n", text);
    exit(1);
}

static void encode_and_find(const JelliLokiOut *message)
{
    char out[JELLI_LOKI_MAX_OUT + 1u];
    size_t size = jelli_loki_encode(message, out, sizeof(out));
    CHECK(size > 0u && size == strlen(out));
    check_sent(out);
    /* One byte short of room for the NUL fails without a partial message. */
    char tight[JELLI_LOKI_MAX_OUT + 1u];
    CHECK(jelli_loki_encode(message, tight, size) == 0u && tight[0] == '\0');
    CHECK(jelli_loki_encode(message, tight, size + 1u) == size);
}

static void encodes_what_the_bridge_decodes(void)
{
    const Vector *ask = find("ask-approve");
    /* Static: initializers below take their addresses. */
    static JelliLokiMessage card;
    static char digest[JELLI_LOKI_DIGEST];
    CHECK(jelli_loki_decode(ask->input, strlen(ask->input), &card) == JELLI_LOKI_OK);
    jelli_loki_card_digest(&card, digest);
    const JelliLokiOut messages[] = {
        {.type = JELLI_LOKI_HELLO,
         .seq = 1u,
         .fw = "0.10.0",
         .board = "1.75c",
         .channels = 1u,
         .frame_ms = 60u},
        {.type = JELLI_LOKI_PTT_START, .seq = 2u, .stream = 1u},
        {.type = JELLI_LOKI_PTT_END, .seq = 3u, .stream = 1u, .frames = 50u, .dropped = 2u},
        {.type = JELLI_LOKI_CANCEL, .seq = 4u},
        {.type = JELLI_LOKI_ANSWER,
         .seq = 5u,
         .id = card.id,
         .nonce = card.nonce,
         .digest = digest,
         .choice = 0u},
        {.type = JELLI_LOKI_PLAYED, .seq = 6u, .reply = 1u, .complete = true},
        {.type = JELLI_LOKI_STATUS, .seq = 7u, .battery = 87, .charging = false, .rssi = -61},
        {.type = JELLI_LOKI_STATUS, .seq = 8u, .battery = -1, .charging = true, .rssi = -127},
    };
    for (size_t i = 0u; i < sizeof(messages) / sizeof(messages[0]); ++i)
        encode_and_find(&messages[i]);
}

static void expect_rejected(const JelliLokiOut *message)
{
    char out[JELLI_LOKI_MAX_OUT + 1u];
    memset(out, 'x', sizeof(out));
    CHECK(jelli_loki_encode(message, out, sizeof(out)) == 0u && out[0] == '\0');
}

static void rejects_bad_hellos(void)
{
    char out[JELLI_LOKI_MAX_OUT + 1u];
    const JelliLokiOut good = {.type = JELLI_LOKI_HELLO,
                               .seq = 1u,
                               .fw = "1",
                               .board = "b",
                               .channels = 1u,
                               .frame_ms = 60u};
    static const char *const FIRMWARE[] = {"1\"", NULL, "1\n", "1\\",
                                           "12345678901234567890123456789012345678901"};
    JelliLokiOut bad = good;
    CHECK(jelli_loki_encode(&good, out, sizeof(out)) > 0u);
    for (size_t i = 0u; i < sizeof(FIRMWARE) / sizeof(FIRMWARE[0]); ++i) {
        bad.fw = FIRMWARE[i];
        expect_rejected(&bad);
    }
    bad = good;
    bad.board = "12345678901234567";
    expect_rejected(&bad);
    bad = good;
    bad.channels = 0u;
    expect_rejected(&bad);
    bad.channels = 5u;
    expect_rejected(&bad);
    bad = good;
    bad.frame_ms = 19u;
    expect_rejected(&bad);
    bad.frame_ms = 101u;
    expect_rejected(&bad);
}

static void rejects_bad_answers(void)
{
    char out[JELLI_LOKI_MAX_OUT + 1u];
    const JelliLokiOut good = {
        .type = JELLI_LOKI_ANSWER,
        .seq = 1u,
        .id = "a",
        .nonce = "00112233445566778899aabbccddeeff",
        .digest = "d9f7edeca33a2b6eb1b3d520c54f673eecfa2aa925af95b16333380873ff7500"};
    static const char *const IDS[] = {"a b", "", NULL, "123456789012345678901234567890123"};
    JelliLokiOut bad = good;
    CHECK(jelli_loki_encode(&good, out, sizeof(out)) > 0u);
    for (size_t i = 0u; i < sizeof(IDS) / sizeof(IDS[0]); ++i) {
        bad.id = IDS[i];
        expect_rejected(&bad);
    }
    bad = good;
    bad.nonce = "00112233445566778899AABBCCDDEEFF";
    expect_rejected(&bad);
    bad = good;
    bad.digest = good.nonce;
    expect_rejected(&bad);
    bad = good;
    bad.choice = 4u;
    expect_rejected(&bad);
}

static void rejects_what_breaks_the_rules(void)
{
    char out[JELLI_LOKI_MAX_OUT + 1u];
    const JelliLokiOut bad[] = {
        {.type = JELLI_LOKI_CANCEL, .seq = 0u},
        {.seq = 1u}, /* No type. */
        {.type = JELLI_LOKI_PTT_START, .seq = 1u, .stream = 0u},
        {.type = JELLI_LOKI_PTT_END, .seq = 1u, .stream = 0u},
        {.type = JELLI_LOKI_PLAYED, .seq = 1u, .reply = 0u},
        {.type = JELLI_LOKI_STATUS, .seq = 1u, .battery = 101, .rssi = -40},
        {.type = JELLI_LOKI_STATUS, .seq = 1u, .battery = -2, .rssi = -40},
        {.type = JELLI_LOKI_STATUS, .seq = 1u, .battery = 50, .rssi = 1},
        {.type = JELLI_LOKI_STATUS, .seq = 1u, .battery = 50, .rssi = -128},
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i)
        expect_rejected(&bad[i]);
    rejects_bad_hellos();
    rejects_bad_answers();
    const JelliLokiOut cancel = {.type = JELLI_LOKI_CANCEL, .seq = 1u};
    CHECK(jelli_loki_encode(NULL, out, sizeof(out)) == 0u);
    CHECK(jelli_loki_encode(&cancel, NULL, 64u) == 0u);
    CHECK(jelli_loki_encode(&cancel, out, 0u) == 0u);
}

static void audio_headers_round_trip(void)
{
    const Vector *mic = find("mic-frame");
    size_t size = loki_unhex(mic->input + 4, bytes, sizeof(bytes));
    uint8_t header[JELLI_LOKI_AUDIO_HEADER];
    JelliLokiAudio audio = {JELLI_LOKI_MIC, false, 1u, 0u}, parsed;
    size_t payload;
    jelli_loki_audio_header(&audio, header);
    CHECK(size == 16u && !memcmp(header, bytes, sizeof(header)));
    CHECK(jelli_loki_audio_parse(bytes, size, JELLI_LOKI_MIC, 1u, &parsed, &payload));
    CHECK(parsed.kind == JELLI_LOKI_MIC && !parsed.final && parsed.stream == 1u &&
          parsed.seq == 0u && payload == 4u);
    /* Two channels need whole sample frames: 4 bytes is one, 6 is not. */
    CHECK(jelli_loki_audio_parse(bytes, size, JELLI_LOKI_MIC, 2u, &parsed, &payload));
    CHECK(!jelli_loki_audio_parse(bytes, size - 2u, JELLI_LOKI_MIC, 2u, &parsed, &payload));
    CHECK(!jelli_loki_audio_parse(bytes, size, JELLI_LOKI_MIC, 0u, &parsed, &payload));
    CHECK(!jelli_loki_audio_parse(bytes, size, JELLI_LOKI_MIC, 5u, &parsed, &payload));
    CHECK(!jelli_loki_audio_parse(NULL, size, JELLI_LOKI_MIC, 1u, &parsed, &payload));
    audio = (JelliLokiAudio){JELLI_LOKI_MIC, true, 7u, 49u};
    jelli_loki_audio_header(&audio, header);
    size = loki_unhex(find("mic-last")->input + 4, bytes, sizeof(bytes));
    CHECK(size == 4812u && !memcmp(header, bytes, sizeof(header)));
}

static void hex_of(const uint8_t digest[JELLI_SHA256_SIZE], char hex[65])
{
    for (size_t i = 0u; i < JELLI_SHA256_SIZE; ++i)
        (void)snprintf(hex + 2u * i, 3u, "%02x", (unsigned)digest[i]);
}

/* FIPS 180-4 examples, plus split updates hashing like one. */
static void sha256_matches_fips(void)
{
    static const struct {
        const char *text, *hex;
    } cases[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    };
    JelliSha256 sha;
    uint8_t digest[JELLI_SHA256_SIZE];
    char hex[65];
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        jelli_sha256_init(&sha);
        jelli_sha256_update(&sha, cases[i].text, strlen(cases[i].text));
        jelli_sha256_final(&sha, digest);
        hex_of(digest, hex);
        CHECK(!strcmp(hex, cases[i].hex));
    }
    static char million[1000000];
    memset(million, 'a', sizeof(million));
    jelli_sha256_init(&sha);
    for (size_t at = 0u, step = 1u; at < sizeof(million); at += step, step = step % 97u + 1u)
        jelli_sha256_update(&sha, million + at,
                            at + step > sizeof(million) ? sizeof(million) - at : step);
    jelli_sha256_final(&sha, digest);
    hex_of(digest, hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

/* The answer vector carries the digest of the ask-approve card. */
static void card_digest_matches_the_vector(void)
{
    const Vector *ask = find("ask-approve"), *answer = find("answer");
    JelliLokiMessage card;
    char digest[JELLI_LOKI_DIGEST];
    CHECK(jelli_loki_decode(ask->input, strlen(ask->input), &card) == JELLI_LOKI_OK);
    jelli_loki_card_digest(&card, digest);
    CHECK(strstr(answer->input, digest) != NULL);
    /* Any change to what the card shows changes the digest. */
    char other[JELLI_LOKI_DIGEST];
    card.options[1][0] = 'd';
    jelli_loki_card_digest(&card, other);
    CHECK(strcmp(digest, other) != 0);
}

static void canonical_needs_room(void)
{
    JelliLokiMessage message;
    char out[64];
    static const char text[] = "{\"t\":\"settings\",\"seq\":16,\"volume\":35}";
    CHECK(jelli_loki_decode(text, strlen(text), &message) == JELLI_LOKI_OK);
    size_t size = jelli_loki_canonical(&message, out, sizeof(out));
    CHECK(size == strlen("settings seq=16 volume=35") && !strcmp(out, "settings seq=16 volume=35"));
    CHECK(jelli_loki_canonical(&message, out, size) == 0u);
    CHECK(jelli_loki_canonical(&message, out, size + 1u) == size);
    CHECK(jelli_loki_canonical(&message, out, 0u) == 0u);
    CHECK(jelli_loki_canonical(&message, NULL, 64u) == 0u);
    message.type = JELLI_LOKI_UNKNOWN;
    CHECK(jelli_loki_canonical(&message, out, sizeof(out)) == 0u);
    CHECK(jelli_loki_decode(NULL, 0u, &message) == JELLI_LOKI_INVALID);
    CHECK(jelli_loki_decode(text, strlen(text), NULL) == JELLI_LOKI_INVALID);
    /* A message is read within its size: the closing brace past it does not count. */
    CHECK(jelli_loki_decode(text, strlen(text) - 1u, &message) == JELLI_LOKI_INVALID);
}

int main(void)
{
    loki_vectors_load();
    decodes_the_vectors();
    encodes_what_the_bridge_decodes();
    rejects_what_breaks_the_rules();
    audio_headers_round_trip();
    sha256_matches_fips();
    card_digest_matches_the_vector();
    canonical_needs_room();
    printf("Loki protocol checks passed (%u vectors, %u decoded)\n", loki_vector_count(), decoded);
    return 0;
}

/* Property tests for the loki/1 decoder (docs/loki-protocol.md): mutated golden
 * vectors, generated messages, and random audio frames. Each input is copied into a
 * buffer of exactly its size, so the sanitizer build catches a read past the end.
 * Seeds are fixed; a failure prints the case number to replay. */
#include "jelli/agent_loki.h"
#include "loki_vectors.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (case %u)\n", __FILE__, __LINE__, #expr, current_case);     \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { MUTATIONS = 120000u, GENERATED = 60000u, FRAMES = 40000u, CAPACITY = 4096u };

static unsigned current_case;
static uint32_t rng = 0x2545f491u;
static unsigned accepted, ignored;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* Strict UTF-8 without control characters; line feeds only where allowed. */
/* The code point of a sequence whose continuation bytes were checked. */
static uint32_t code_point(const unsigned char *at, size_t extra)
{
    static const unsigned char LEAD_BITS[] = {0x7Fu, 0x1Fu, 0x0Fu, 0x07u};
    uint32_t code = at[0] & LEAD_BITS[extra];
    for (size_t k = 1u; k <= extra; ++k)
        code = code << 6 | (at[k] & 0x3Fu);
    return code;
}

/* Strict UTF-8 without control characters; line feeds only where allowed. */
static bool clean_text(const char *text, size_t max_bytes, bool line_feeds)
{
    static const uint32_t SMALLEST[] = {0u, 0x80u, 0x800u, 0x10000u};
    const unsigned char *at = (const unsigned char *)text;
    size_t length = strlen(text);
    if (length > max_bytes)
        return false;
    for (size_t i = 0u; i < length;) {
        unsigned char c = at[i];
        size_t extra = c < 0x80u ? 0u : c >= 0xF0u ? 3u : c >= 0xE0u ? 2u : 1u;
        if ((c < 0x20u && !(c == '\n' && line_feeds)) || (c >= 0x80u && c < 0xC2u) || c > 0xF4u ||
            (extra && i + extra >= length))
            return false;
        for (size_t k = 1u; k <= extra; ++k)
            if ((at[i + k] & 0xC0u) != 0x80u)
                return false;
        uint32_t code = code_point(at + i, extra);
        if (code < SMALLEST[extra] || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu))
            return false;
        i += extra + 1u;
    }
    return true;
}

static bool terminated(const char *member, size_t size)
{
    return memchr(member, '\0', size) != NULL;
}

static bool charset(const char *text, size_t min, size_t max, bool hex)
{
    size_t length = strlen(text);
    if (length < min || length > max)
        return false;
    for (size_t i = 0u; i < length; ++i) {
        char c = text[i];
        bool digit = c >= '0' && c <= '9', lower = c >= 'a' && c <= 'z';
        if (hex ? !(digit || (c >= 'a' && c <= 'f'))
                : !(digit || lower || (c >= 'A' && c <= 'Z') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

static void check_strings(const JelliLokiMessage *m)
{
    CHECK(terminated(m->bridge, sizeof(m->bridge)) && terminated(m->voice, sizeof(m->voice)));
    CHECK(terminated(m->text, sizeof(m->text)) && terminated(m->id, sizeof(m->id)));
    CHECK(terminated(m->nonce, sizeof(m->nonce)) && terminated(m->title, sizeof(m->title)));
    CHECK(terminated(m->detail, sizeof(m->detail)));
    CHECK(clean_text(m->bridge, 40u, false) && clean_text(m->voice, 40u, false));
    CHECK(clean_text(m->title, 120u, false) && clean_text(m->detail, 600u, true));
    CHECK(clean_text(m->text, m->type == JELLI_LOKI_STATE ? 200u : 400u, true));
    for (unsigned i = 0u; i < JELLI_LOKI_OPTIONS; ++i)
        CHECK(terminated(m->options[i], sizeof(m->options[i])) &&
              clean_text(m->options[i], 40u, false));
}

static void check_type(const JelliLokiMessage *m)
{
    switch (m->type) {
    case JELLI_LOKI_WELCOME:
        CHECK(m->proto == 1u);
        break;
    case JELLI_LOKI_ASK: {
        char digest[JELLI_LOKI_DIGEST];
        CHECK(charset(m->id, 1u, 32u, false) && charset(m->nonce, 32u, 32u, true));
        CHECK(m->option_count >= 2u && m->option_count <= 4u);
        CHECK(m->kind == JELLI_LOKI_CHOOSE || m->option_count == 2u);
        CHECK(m->timeout_ms >= 1000u && m->timeout_ms <= 600000u);
        jelli_loki_card_digest(m, digest);
        CHECK(strlen(digest) == 64u && charset(digest, 64u, 64u, true));
        break;
    }
    case JELLI_LOKI_ASK_CANCEL:
        CHECK(charset(m->id, 1u, 32u, false));
        break;
    case JELLI_LOKI_REPLY_BEGIN:
        CHECK(m->reply >= 1u && m->rate == JELLI_LOKI_RATE);
        break;
    case JELLI_LOKI_REPLY_END:
        CHECK(m->reply >= 1u);
        break;
    case JELLI_LOKI_UNKNOWN:
    case JELLI_LOKI_STATE:
    case JELLI_LOKI_CAPTION:
    case JELLI_LOKI_STOP_AUDIO:
    case JELLI_LOKI_SETTINGS:
        break;
    }
}

/* Decodes size bytes twice from an exact-size copy and checks what came out. */
static void decode_case(const uint8_t *input, size_t size)
{
    static JelliLokiMessage first, second;
    static char canonical[16384];
    char *copy = malloc(size ? size : 1u);
    CHECK(copy != NULL);
    if (size)
        memcpy(copy, input, size);
    JelliLokiResult result = jelli_loki_decode(copy, size, &first);
    CHECK(jelli_loki_decode(copy, size, &second) == result);
    free(copy);
    if (result == JELLI_LOKI_INVALID)
        return;
    CHECK(first.type == second.type && first.seq == second.seq && first.seq >= 1u);
    if (result == JELLI_LOKI_IGNORED) {
        CHECK(first.type == JELLI_LOKI_UNKNOWN);
        ++ignored;
        return;
    }
    CHECK(first.type >= JELLI_LOKI_WELCOME && first.type <= JELLI_LOKI_SETTINGS);
    CHECK(first.state <= JELLI_LOKI_STATE_FAILURE && first.kind <= JELLI_LOKI_CHOOSE);
    CHECK(first.progress >= -1 && first.progress <= 1000 && first.queue <= 999u);
    CHECK(first.volume <= 100u && size <= JELLI_LOKI_MAX_IN);
    check_strings(&first);
    check_type(&first);
    /* Decoding the same bytes again means the same message. */
    static char again[16384];
    CHECK(jelli_loki_canonical(&first, canonical, sizeof(canonical)) > 0u);
    CHECK(jelli_loki_canonical(&second, again, sizeof(again)) > 0u && !strcmp(canonical, again));
    ++accepted;
}

static const char INTERESTING[] = "{}[]\":,\\/ \n\r\t0123456789-+.eEtrufalsnu";

static uint8_t pick_byte(void)
{
    static const uint8_t special[] = {0x00u, 0x7Fu, 0x80u, 0xC0u, 0xC3u, 0xA9u,
                                      0xEDu, 0xA0u, 0xF0u, 0xF4u, 0x90u, 0xFFu};
    switch (next() % 3u) {
    case 0u:
        return (uint8_t)INTERESTING[next() % (sizeof(INTERESTING) - 1u)];
    case 1u:
        return special[next() % sizeof(special)];
    default:
        return (uint8_t)next();
    }
}

static size_t mutate(uint8_t *buffer, size_t size)
{
    for (unsigned edits = 1u + next() % 4u; edits; --edits) {
        size_t at = size ? next() % size : 0u;
        switch (next() % 6u) {
        case 0u:
        case 1u:
            if (size)
                buffer[at] = pick_byte();
            break;
        case 2u:
            if (size < CAPACITY) {
                memmove(buffer + at + 1u, buffer + at, size - at);
                buffer[at] = pick_byte();
                ++size;
            }
            break;
        case 3u:
            if (size) {
                memmove(buffer + at, buffer + at + 1u, size - at - 1u);
                --size;
            }
            break;
        case 4u: { /* Repeat a slice, such as a member, in place. */
            size_t length = size - at < 24u ? size - at : 1u + next() % 24u;
            if (size + length <= CAPACITY && length) {
                memmove(buffer + at + length, buffer + at, size - at);
                size += length;
            }
            break;
        }
        default:
            size = at;
            break;
        }
    }
    return size;
}

static void mutated_vectors(void)
{
    static uint8_t buffer[CAPACITY], scratch[CAPACITY];
    unsigned controls[512], count = 0u;
    for (unsigned i = 0u; i < loki_vector_count() && count < 512u; ++i)
        if (!strcmp(loki_vector(i)->direction, "b2d") &&
            strncmp(loki_vector(i)->input, "hex:", 4u) != 0)
            controls[count++] = i;
    CHECK(count > 100u);
    for (current_case = 0u; current_case < MUTATIONS; ++current_case) {
        size_t size;
        const char *text = loki_control_bytes(loki_vector(controls[next() % count]), scratch,
                                              sizeof(scratch), &size);
        size = size < CAPACITY ? size : CAPACITY;
        memcpy(buffer, text, size);
        decode_case(buffer, mutate(buffer, size));
    }
}

/* A value that is often what a field wants and often subtly not. */
static void append_value(char *out, size_t capacity)
{
    static const char *const VALUES[] = {"0",
                                         "1",
                                         "-1",
                                         "-0",
                                         "01",
                                         "2",
                                         "35",
                                         "100",
                                         "101",
                                         "999",
                                         "1000",
                                         "1001",
                                         "24000",
                                         "600000",
                                         "4294967295",
                                         "4294967296",
                                         "1e3",
                                         "1.5",
                                         "true",
                                         "false",
                                         "null",
                                         "\"\"",
                                         "\"idle\"",
                                         "\"failure\"",
                                         "\"approve\"",
                                         "\"choose\"",
                                         "\"a1\"",
                                         "\"Scar\"",
                                         "\"00112233445566778899aabbccddeeff\"",
                                         "\"line\\nfeed\"",
                                         "\"tab\\tx\"",
                                         "\"\\u00e9\\ud83e\\udd84\"",
                                         "\"\\ud83e\"",
                                         "\"\\u0000\"",
                                         "\"q\\\"\\\\\"",
                                         "[\"Allow\",\"Deny\"]",
                                         "[\"a\",\"b\",\"c\",\"d\"]",
                                         "[\"a\",\"b\",\"c\",\"d\",\"e\"]",
                                         "[]",
                                         "[\"a\",2]",
                                         "{}",
                                         "{\"a\":[1,{\"b\":[[[]]]}]}",
                                         "[[[[[0]]]]]"};
    size_t used = strlen(out);
    if (next() % 4u == 0u) { /* A random printable string, sometimes long. */
        size_t length = next() % 3u ? next() % 48u : next() % 700u;
        if (used + length + 3u < capacity) {
            out[used++] = '"';
            for (size_t i = 0u; i < length; ++i) {
                char c = (char)(' ' + next() % 95u);
                out[used++] = c == '"' || c == '\\' ? 'x' : c;
            }
            out[used++] = '"';
            out[used] = '\0';
        }
        return;
    }
    const char *value = VALUES[next() % (sizeof(VALUES) / sizeof(VALUES[0]))];
    if (used + strlen(value) < capacity)
        memcpy(out + used, value, strlen(value) + 1u);
}

static void generated_messages(void)
{
    static const char *const TYPES[] = {"welcome",    "state",       "caption",   "ask",
                                        "ask_cancel", "reply_begin", "reply_end", "stop_audio",
                                        "settings",   "confetti",    "hello"};
    static const char *const KEYS[] = {
        "seq",        "proto", "bridge", "voice",  "state",  "caption", "progress", "queue",
        "text",       "final", "id",     "nonce",  "kind",   "title",   "detail",   "options",
        "timeout_ms", "reply", "rate",   "frames", "volume", "t",       "zzz"};
    static char text[CAPACITY];
    for (current_case = 0u; current_case < GENERATED; ++current_case) {
        (void)snprintf(text, sizeof(text), "{\"t\":\"%s\",\"seq\":%u",
                       TYPES[next() % (sizeof(TYPES) / sizeof(TYPES[0]))], 1u + next() % 9u);
        for (unsigned members = next() % 9u; members; --members) {
            size_t used = strlen(text);
            (void)snprintf(text + used, sizeof(text) - used,
                           ",\"%s\":", KEYS[next() % (sizeof(KEYS) / sizeof(KEYS[0]))]);
            append_value(text, sizeof(text) - 2u);
        }
        size_t used = strlen(text);
        text[used] = '}';
        decode_case((const uint8_t *)text, used + 1u);
    }
}

static void audio_frames(void)
{
    static uint8_t frame[JELLI_LOKI_AUDIO_HEADER + JELLI_LOKI_AUDIO_MAX + 64u];
    unsigned valid = 0u;
    for (current_case = 0u; current_case < FRAMES; ++current_case) {
        size_t size = next() % 3u ? next() % 64u : next() % sizeof(frame);
        for (size_t i = 0u; i < size; ++i)
            frame[i] = (uint8_t)next();
        if (size >= 4u && next() % 4u) { /* Mostly a plausible header. */
            frame[0] = 'L';
            frame[1] = 'K';
            frame[2] = (uint8_t)(1u + next() % 2u);
            frame[3] = (uint8_t)(next() % 3u);
        }
        JelliLokiAudioKind kind = next() % 2u ? JELLI_LOKI_MIC : JELLI_LOKI_REPLY;
        unsigned channels = next() % 6u;
        uint8_t *copy = malloc(size ? size : 1u);
        CHECK(copy != NULL);
        if (size)
            memcpy(copy, frame, size);
        JelliLokiAudio audio;
        size_t payload = 0u;
        if (jelli_loki_audio_parse(copy, size, kind, channels, &audio, &payload)) {
            uint8_t header[JELLI_LOKI_AUDIO_HEADER];
            CHECK(size > JELLI_LOKI_AUDIO_HEADER && payload == size - JELLI_LOKI_AUDIO_HEADER);
            CHECK(payload <= JELLI_LOKI_AUDIO_MAX && channels >= 1u && channels <= 4u);
            CHECK(payload % ((size_t)2u * channels) == 0u && audio.kind == kind &&
                  audio.stream != 0u);
            jelli_loki_audio_header(&audio, header);
            CHECK(!memcmp(header, copy, sizeof(header)));
            ++valid;
        }
        free(copy);
    }
    CHECK(valid > 100u);
}

int main(void)
{
    loki_vectors_load();
    mutated_vectors();
    unsigned from_vectors = accepted;
    generated_messages();
    audio_frames();
    current_case = 0u;
    CHECK(from_vectors > 1000u && accepted > from_vectors + 100u && ignored > 100u);
    printf("Loki fuzz checks passed (%u accepted, %u ignored)\n", accepted, ignored);
    return 0;
}

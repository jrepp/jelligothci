/* Property tests for model choice in loki/1 (ADR-018, docs/loki-protocol.md): generated
 * models messages, decoded from exact-size copies, against a reference written from
 * the protocol's words rather than the decoder's code; the canonical writer on lists
 * built by hand; and set_model at every name and every output capacity. Seeds are
 * fixed; a failure prints the case number to replay. */
#include "jelli/agent_loki.h"
#include <inttypes.h>
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

enum { CASES = 60000u, NAMES = 18u, NAME_BYTES = 44u, TEXT = 2048u, CANARY = 0x5A };

static const char ALLOWED[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._:/+-";
static unsigned current_case, accepted, refused;
static uint32_t rng = 0x6d2b79f5u;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* The protocol's rule, from its words. */
static bool is_model_name(const char *name)
{
    size_t length = strlen(name);
    return length >= 1u && length <= 40u && strspn(name, ALLOWED) == length;
}

/* A name that is mostly allowed and sometimes not: a space, a quote, a backslash, a
 * comma, a byte of UTF-8, or one character too many. */
static void random_name(char name[NAME_BYTES])
{
    static const char *const ODD[] = {" ", "\"", "\\", ",", "\xc3\xa9", "\x7f", "*", "~"};
    size_t length = next() % 8u == 0u ? 40u + next() % 3u : next() % 24u;
    size_t used = 0u;
    while (used < length && used + 2u < NAME_BYTES) {
        if (next() % 40u == 0u) {
            const char *odd = ODD[next() % (sizeof(ODD) / sizeof(ODD[0]))];
            memcpy(name + used, odd, strlen(odd));
            used += strlen(odd);
        } else {
            name[used++] = ALLOWED[next() % (sizeof(ALLOWED) - 1u)];
        }
    }
    name[used < NAME_BYTES ? used : NAME_BYTES - 1u] = '\0';
}

/* Appends text to the string in out, or exits when it does not fit. */
static void append(char *out, size_t capacity, const char *text)
{
    size_t used = strlen(out), length = strlen(text);
    CHECK(length < capacity - used);
    memcpy(out + used, text, length + 1u);
}

/* The name as a JSON string: quotes and backslashes escaped, the rest as is. */
static void put_json_string(char *out, size_t capacity, const char *name)
{
    size_t used = strlen(out);
    out[used++] = '"';
    for (const char *at = name; *at && used + 3u < capacity; ++at) {
        if (*at == '"' || *at == '\\')
            out[used++] = '\\';
        out[used++] = *at;
    }
    out[used++] = '"';
    out[used] = '\0';
}

typedef struct {
    char names[NAMES][NAME_BYTES];
    unsigned count, current;
    bool has_models, has_current;
} Offer;

static void random_offer(Offer *offer)
{
    offer->count = next() % 4u == 0u ? next() % NAMES : 1u + next() % 4u;
    for (unsigned i = 0u; i < offer->count; ++i)
        random_name(offer->names[i]);
    offer->current = next() % 3u ? (offer->count ? next() % offer->count : 0u) : next() % 18u;
    offer->has_models = next() % 16u != 0u;
    offer->has_current = next() % 16u != 0u;
}

/* The message, with its members in either order. */
static size_t write_offer(const Offer *offer, char *text, size_t capacity)
{
    char current[32];
    (void)snprintf(text, capacity, "{\"t\":\"models\",\"seq\":%u", 1u + next() % 9u);
    (void)snprintf(current, sizeof(current), ",\"current\":%u", offer->current);
    bool current_first = next() & 1u;
    if (offer->has_current && current_first)
        append(text, capacity, current);
    if (offer->has_models) {
        append(text, capacity, ",\"models\":[");
        for (unsigned i = 0u; i < offer->count; ++i) {
            if (i)
                append(text, capacity, ",");
            put_json_string(text, capacity, offer->names[i]);
        }
        append(text, capacity, "]");
    }
    if (offer->has_current && !current_first)
        append(text, capacity, current);
    append(text, capacity, "}");
    return strlen(text);
}

static bool offer_valid(const Offer *offer)
{
    if (!offer->has_models || !offer->has_current || offer->count < 1u ||
        offer->count > JELLI_LOKI_MODEL_MAX || offer->current >= offer->count)
        return false;
    for (unsigned i = 0u; i < offer->count; ++i)
        if (!is_model_name(offer->names[i]))
            return false;
    return true;
}

/* models seq=N models=["a","b"] current=C, as the protocol writes it. */
static void expected_canonical(const Offer *offer, uint32_t seq, char *out, size_t capacity)
{
    (void)snprintf(out, capacity, "models seq=%" PRIu32 " models=[", seq);
    for (unsigned i = 0u; i < offer->count; ++i) {
        if (i)
            append(out, capacity, ",");
        put_json_string(out, capacity, offer->names[i]); /* No escapes: names have none. */
    }
    size_t used = strlen(out);
    (void)snprintf(out + used, capacity - used, "] current=%u", offer->current);
}

static void check_decoded(const Offer *offer, const JelliLokiMessage *message)
{
    static char canonical[TEXT], expected[TEXT];
    CHECK(message->type == JELLI_LOKI_MODELS && message->model_count == offer->count);
    CHECK(message->current == offer->current);
    for (unsigned i = 0u; i < JELLI_LOKI_MODEL_MAX; ++i) /* Unused names stay empty. */
        CHECK(i < offer->count ? !strcmp(message->models[i], offer->names[i])
                               : message->models[i][0] == '\0');
    CHECK(!message->option_count && !message->volume && message->progress == -1);
    expected_canonical(offer, message->seq, expected, sizeof(expected));
    CHECK(jelli_loki_canonical(message, canonical, sizeof(canonical)) == strlen(expected));
    CHECK(!strcmp(canonical, expected));
    /* Each name offered can be sent back as the choice. */
    char json[JELLI_LOKI_MAX_OUT + 1u];
    JelliLokiOut choice = {.type = JELLI_LOKI_SET_MODEL, .seq = 1u};
    choice.model = message->models[next() % message->model_count];
    CHECK(jelli_loki_encode(&choice, json, sizeof(json)) > 0u);
}

static void generated_offers(void)
{
    static char text[TEXT];
    static JelliLokiMessage message;
    for (current_case = 0u; current_case < CASES; ++current_case) {
        Offer offer;
        random_offer(&offer);
        size_t size = write_offer(&offer, text, sizeof(text));
        char *copy = malloc(size);
        CHECK(copy != NULL);
        memcpy(copy, text, size);
        JelliLokiResult result = jelli_loki_decode(copy, size, &message);
        free(copy);
        bool valid = offer_valid(&offer);
        if (result != (valid ? JELLI_LOKI_OK : JELLI_LOKI_INVALID))
            fprintf(stderr, "decoded %d: %s\n", (int)result, text);
        CHECK(result == (valid ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
        if (valid) {
            check_decoded(&offer, &message);
            ++accepted;
        } else {
            ++refused;
        }
    }
}

/* Hand-built messages: a count past the capacity writes only what the list holds. */
static void canonical_cuts_the_list(void)
{
    static JelliLokiMessage message;
    static char out[TEXT];
    current_case = 0u;
    memset(&message, 0, sizeof(message));
    message.type = JELLI_LOKI_MODELS;
    message.seq = 3u;
    for (unsigned i = 0u; i < JELLI_LOKI_MODEL_MAX; ++i)
        (void)snprintf(message.models[i], sizeof(message.models[i]), "m%u", i);
    message.model_count = 99u;
    message.current = 7u;
    CHECK(jelli_loki_canonical(&message, out, sizeof(out)) > 0u);
    CHECK(strstr(out, "\"m15\"] current=7") && !strstr(out, "m16"));
    message.model_count = 0u;
    CHECK(jelli_loki_canonical(&message, out, sizeof(out)) > 0u);
    CHECK(!strcmp(out, "models seq=3 models=[] current=7"));
}

/* set_model at every capacity: a short one returns 0, leaves an empty string, and
 * writes nothing past it; any name the rule refuses is never sent. */
static void set_model_everywhere(void)
{
    static char out[JELLI_LOKI_MAX_OUT + 16u];
    char json[JELLI_LOKI_MAX_OUT + 1u], expected[128], name[NAME_BYTES];
    for (current_case = 0u; current_case < 4000u; ++current_case) {
        random_name(name);
        JelliLokiOut message = {.type = JELLI_LOKI_SET_MODEL, .seq = 1u + next() % 99u};
        message.model = name;
        size_t size = jelli_loki_encode(&message, json, sizeof(json));
        CHECK(jelli_loki_model_name(name, strlen(name)) == is_model_name(name));
        if (!is_model_name(name)) {
            CHECK(size == 0u && json[0] == '\0');
            continue;
        }
        (void)snprintf(expected, sizeof(expected),
                       "{\"t\":\"set_model\",\"seq\":%" PRIu32 ",\"model\":\"%s\"}", message.seq,
                       name);
        CHECK(size == strlen(expected) && !strcmp(json, expected));
        for (size_t capacity = 0u; capacity <= size + 1u; capacity += 1u + next() % 8u) {
            memset(out, CANARY, sizeof(out));
            CHECK(jelli_loki_encode(&message, out, capacity) == (capacity > size ? size : 0u));
            CHECK(capacity == 0u || capacity > size || out[0] == '\0');
            for (size_t i = capacity; i < sizeof(out); ++i)
                CHECK(out[i] == (char)CANARY);
        }
    }
    CHECK(!jelli_loki_model_name(NULL, 1u) && !jelli_loki_model_name("abc", 0u));
    CHECK(jelli_loki_model_name("abc", 2u) && !jelli_loki_model_name("a b", 3u));
}

int main(void)
{
    generated_offers();
    canonical_cuts_the_list();
    set_model_everywhere();
    current_case = 0u;
    CHECK(accepted > 10000u && refused > 10000u);
    printf("Loki model checks passed (%u accepted, %u refused)\n", accepted, refused);
    return 0;
}

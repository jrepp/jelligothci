/* set_model and model lists end to end (ADR-018): the encoder refuses every name the
 * protocol does not allow and writes only printable ASCII; every models and set_model
 * vector of tests/vectors/loki-1.tsv round-trips through its canonical form and the
 * sender's bytes; and every allowed name survives the board's console, which takes
 * printable ASCII words only, so "loki model NAME" reaches the encoder as typed. */
#include "jelli/agent_console.h"
#include "jelli/agent_loki.h"
#include "loki_vectors.h"
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

enum { TEXT = 4096u };

static const char ALLOWED[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._:/+-";
static unsigned current_case, round_trips;

static size_t encode(const char *model, uint32_t seq, char *out, size_t capacity)
{
    JelliLokiOut message = {.type = JELLI_LOKI_SET_MODEL, .seq = seq, .model = model};
    return jelli_loki_encode(&message, out, capacity);
}

static bool printable(const char *text)
{
    for (; *text; ++text)
        if (*text < ' ' || *text > '~')
            return false;
    return true;
}

/* Each byte alone, and between two allowed ones, is sent exactly when it is allowed. */
static void every_byte(void)
{
    char json[JELLI_LOKI_MAX_OUT + 1u], expected[128], name[4];
    for (unsigned c = 1u; c <= 0xffu; ++c) {
        current_case = c;
        bool allowed = c < 0x80u && strchr(ALLOWED, (int)c) != NULL;
        name[0] = (char)c;
        name[1] = '\0';
        size_t size = encode(name, 3u, json, sizeof(json));
        CHECK((size > 0u) == allowed);
        CHECK(jelli_loki_model_name(name, 1u) == allowed);
        if (allowed) {
            (void)snprintf(expected, sizeof(expected),
                           "{\"t\":\"set_model\",\"seq\":3,\"model\":\"%c\"}", (char)c);
            CHECK(size == strlen(expected) && !strcmp(json, expected) && printable(json));
        } else {
            CHECK(json[0] == '\0');
        }
        name[0] = 'a';
        name[1] = (char)c;
        name[2] = 'z';
        name[3] = '\0';
        CHECK((encode(name, 3u, json, sizeof(json)) > 0u) == allowed);
    }
}

/* Lengths, a missing name, seq, the fields of other types, and the name's own length. */
static void names_and_fields(void)
{
    char json[JELLI_LOKI_MAX_OUT + 1u], name[64];
    for (size_t length = 0u; length <= 42u; ++length) {
        current_case = (unsigned)length;
        for (size_t i = 0u; i < length; ++i)
            name[i] = ALLOWED[(i * 7u + length) % (sizeof(ALLOWED) - 1u)];
        name[length] = '\0';
        size_t size = encode(name, 1u, json, sizeof(json));
        CHECK((size > 0u) == (length >= 1u && length <= 40u));
        CHECK(size == 0u ||
              size == strlen("{\"t\":\"set_model\",\"seq\":1,\"model\":\"\"}") + length);
    }
    current_case = 0u;
    CHECK(encode(NULL, 1u, json, sizeof(json)) == 0u && json[0] == '\0');
    CHECK(encode("Claude", 0u, json, sizeof(json)) == 0u); /* seq starts at 1. */
    CHECK(encode("Claude", UINT32_MAX, json, sizeof(json)) > 0u);
    CHECK(!strcmp(json, "{\"t\":\"set_model\",\"seq\":4294967295,\"model\":\"Claude\"}"));
    /* Other types' fields are not read, and model is not read by other types. */
    JelliLokiOut full = {.type = JELLI_LOKI_SET_MODEL,
                         .seq = 2u,
                         .model = "m",
                         .fw = "x y",
                         .id = "\"",
                         .battery = 900,
                         .choice = 99u,
                         .rssi = 5};
    CHECK(jelli_loki_encode(&full, json, sizeof(json)) > 0u);
    CHECK(!strcmp(json, "{\"t\":\"set_model\",\"seq\":2,\"model\":\"m\"}"));
    JelliLokiOut cancel = {.type = JELLI_LOKI_CANCEL, .seq = 2u, .model = "a b"};
    CHECK(jelli_loki_encode(&cancel, json, sizeof(json)) > 0u && !strstr(json, "model"));
    /* jelli_loki_model_name reads exactly length bytes: a prefix, never past it. */
    CHECK(jelli_loki_model_name("a b", 1u) && !jelli_loki_model_name("a b", 2u));
    CHECK(!jelli_loki_model_name("ab\0", 3u) && !jelli_loki_model_name("", 0u));
    memset(name, 'q', sizeof(name));
    CHECK(jelli_loki_model_name(name, 40u) && !jelli_loki_model_name(name, 41u));
    CHECK(!jelli_loki_model_name(name, SIZE_MAX));
}

/* The value after key in a sender's JSON: digits, or the bytes of a string with no
 * escapes (model names have none). */
static void json_value(const char *json, const char *key, char *out, size_t capacity)
{
    char quoted[48];
    (void)snprintf(quoted, sizeof(quoted), "\"%s\":", key);
    const char *at = strstr(json, quoted);
    CHECK(at != NULL);
    at += strlen(quoted);
    bool string = *at == '"';
    if (string)
        ++at;
    size_t length = string ? strcspn(at, "\"") : strspn(at, "0123456789");
    CHECK(length < capacity);
    memcpy(out, at, length);
    out[length] = '\0';
}

/* A models message as the bridge writes it: keys in order and no spaces. */
static void sender_form(const JelliLokiMessage *m, char *out, size_t capacity)
{
    size_t used = (size_t)snprintf(out, capacity,
                                   "{\"t\":\"models\",\"seq\":%" PRIu32 ",\"models\":[", m->seq);
    for (unsigned i = 0u; i < m->model_count; ++i)
        used +=
            (size_t)snprintf(out + used, capacity - used, "%s\"%s\"", i ? "," : "", m->models[i]);
    (void)snprintf(out + used, capacity - used, "],\"current\":%" PRIu32 "}", m->current);
}

/* A models vector: decoded, its canonical form as the vector says, written back as the
 * sender's bytes, and decoded again to the same message. */
static void models_round_trip(const LokiVector *v)
{
    static JelliLokiMessage first, second;
    static char canonical[TEXT], again[TEXT], json[TEXT];
    CHECK(jelli_loki_decode(v->input, strlen(v->input), &first) == JELLI_LOKI_OK);
    CHECK(jelli_loki_canonical(&first, canonical, sizeof(canonical)) > 0u);
    CHECK(!strcmp(canonical, v->canonical));
    sender_form(&first, json, sizeof(json));
    CHECK(!strcmp(json, v->input));
    CHECK(jelli_loki_decode(json, strlen(json), &second) == JELLI_LOKI_OK);
    CHECK(first.type == second.type && first.seq == second.seq);
    CHECK(first.model_count == second.model_count && first.current == second.current);
    CHECK(!memcmp(first.models, second.models, sizeof(first.models)));
    CHECK(jelli_loki_canonical(&second, again, sizeof(again)) > 0u && !strcmp(again, canonical));
    for (unsigned i = 0u; i < first.model_count; ++i) { /* Each can be picked back. */
        char out[JELLI_LOKI_MAX_OUT + 1u];
        CHECK(encode(first.models[i], 1u, out, sizeof(out)) > 0u && printable(out));
    }
}

/* A set_model vector: the board writes its input byte for byte, and its canonical form
 * names the same model. */
static void set_model_round_trip(const LokiVector *v)
{
    char model[64], seq[16], out[JELLI_LOKI_MAX_OUT + 1u], canonical[128];
    json_value(v->input, "model", model, sizeof(model));
    json_value(v->input, "seq", seq, sizeof(seq));
    uint32_t number = 0u;
    CHECK(jelli_agent_number(seq, &number));
    CHECK(encode(model, number, out, sizeof(out)) == strlen(v->input) && !strcmp(out, v->input));
    (void)snprintf(canonical, sizeof(canonical), "set_model seq=%s model=\"%s\"", seq, model);
    CHECK(!strcmp(canonical, v->canonical));
}

static void vectors(void)
{
    loki_vectors_load();
    unsigned models = 0u, set_models = 0u, refused = 0u;
    for (current_case = 0u; current_case < loki_vector_count(); ++current_case) {
        const LokiVector *v = loki_vector(current_case);
        bool ok = !strcmp(v->verdict, "ok");
        if (!strncmp(v->input, "{\"t\":\"models\"", 13u) && !strcmp(v->direction, "b2d")) {
            if (ok) {
                models_round_trip(v);
                ++models;
            }
        } else if (!strncmp(v->input, "{\"t\":\"set_model\"", 16u) &&
                   !strcmp(v->direction, "d2b")) {
            if (ok) {
                set_model_round_trip(v);
                ++set_models;
            } else if (strstr(v->input, "\"model\":\"")) {
                char model[64];
                json_value(v->input, "model", model, sizeof(model));
                char out[JELLI_LOKI_MAX_OUT + 1u]; /* What the bridge refuses, never sent. */
                CHECK(encode(model, 1u, out, sizeof(out)) == 0u);
                ++refused;
            }
        }
    }
    current_case = 0u;
    CHECK(models >= 4u && set_models >= 4u && refused >= 3u);
    round_trips = models + set_models;
}

/* "loki model NAME" through the console: every allowed name arrives as one word,
 * unchanged, and a name with a space would arrive as two (so the bridge never gets it). */
static void through_the_console(void)
{
    static JelliAgentConsole console;
    char name[48], line[96];
    for (current_case = 0u; current_case < 400u; ++current_case) {
        size_t length = 1u + current_case % 40u;
        for (size_t i = 0u; i < length; ++i)
            name[i] = ALLOWED[((size_t)current_case * 31u + i * 17u) % (sizeof(ALLOWED) - 1u)];
        name[length] = '\0';
        (void)snprintf(line, sizeof(line), "@A1 %u loki model %s\n", current_case + 1u, name);
        JelliAgentRequest request;
        bool ready = false;
        for (const char *at = line; *at; ++at)
            ready = jelli_agent_console_feed(&console, *at, &request);
        CHECK(ready && request.count == 3u && !strcmp(request.words[2], name));
        char out[JELLI_LOKI_MAX_OUT + 1u];
        CHECK(encode(request.words[2], 1u, out, sizeof(out)) > 0u && printable(out));
    }
    JelliAgentRequest request;
    bool ready = false;
    for (const char *at = "@A1 9 loki model gpt oss\n"; *at; ++at)
        ready = jelli_agent_console_feed(&console, *at, &request);
    CHECK(ready && request.count == 4u && !strcmp(request.words[2], "gpt"));
}

int main(void)
{
    every_byte();
    names_and_fields();
    vectors();
    through_the_console();
    printf("Loki set_model checks passed (%u vectors round-tripped)\n", round_trips);
    return 0;
}

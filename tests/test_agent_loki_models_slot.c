/* The firmware's slot for the bridge's latest models message (ADR-018,
 * ports/esp32-agent/main/loki_models.c), built on the host with its FreeRTOS spinlock
 * as a mutex (tests/fake_freertos) and the link stubbed here: what it keeps and refuses,
 * when its generation moves, the console's "loki models" JSON at the console's buffer
 * size and at every smaller one, "loki model NAME" for listed and unlisted names, and
 * writers, a forgetter, and readers racing it without a torn copy. */
#include "../ports/esp32-agent/main/loki_link.h"
#include "../ports/esp32-agent/main/loki_models.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: case %u: %s\n", __FILE__, __LINE__, current_case, #expr);      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { CONSOLE_BODY = 768, RACE = 40000 }; /* console_wire.c's BODY_BYTES. */

static unsigned current_case;
static bool online, sendable;
static unsigned sent;
static char sent_model[64];

/* The link, stubbed: online as set, and a send that records set_model. */
bool jelli_agent_link_online(void) { return online; }

bool jelli_agent_link_send(JelliLokiOut *message)
{
    char json[JELLI_LOKI_MAX_OUT + 1u];
    if (!sendable || message->type != JELLI_LOKI_SET_MODEL)
        return false;
    message->seq = 1u;
    CHECK(jelli_loki_encode(message, json, sizeof(json)) > 0u);
    (void)snprintf(sent_model, sizeof(sent_model), "%s", message->model);
    ++sent;
    return true;
}

static JelliLokiMessage decoded(const char *json)
{
    JelliLokiMessage message;
    CHECK(jelli_loki_decode(json, strlen(json), &message) == JELLI_LOKI_OK);
    return message;
}

/* Only a models message loki/1 allows is kept; each offer and each forget of a list
 * moves the generation, and nothing else does. */
static void keeps_and_refuses(void)
{
    JelliPrefsModels got;
    uint32_t start = jelli_agent_models_get(&got);
    CHECK(got.count == 0u && start == jelli_agent_models_generation());
    JelliLokiMessage good = decoded("{\"t\":\"models\",\"seq\":1,\"models\":[\"Claude\","
                                    "\"gpt-oss:20b\",\"hf.co/a+b\"],\"current\":2}");
    jelli_agent_models_offer(NULL);
    for (current_case = 0u; current_case < 5u; ++current_case) {
        JelliLokiMessage bad = good; /* Each breaks one rule of loki/1. */
        if (current_case == 0u)
            bad.type = JELLI_LOKI_SETTINGS;
        else if (current_case < 3u)
            bad.model_count = current_case == 1u ? 0u : JELLI_LOKI_MODEL_MAX + 1u;
        else
            bad.current = current_case == 3u ? 3u : UINT32_MAX;
        jelli_agent_models_offer(&bad);
    }
    CHECK(jelli_agent_models_generation() == start && jelli_agent_models_get(&got) == start);
    CHECK(got.count == 0u);
    jelli_agent_models_forget(); /* Nothing to forget: nothing changes. */
    CHECK(jelli_agent_models_generation() == start);
    jelli_agent_models_offer(&good);
    CHECK(jelli_agent_models_get(&got) == start + 1u && got.count == 3u && got.current == 2u);
    CHECK(!strcmp(got.names[0], "Claude") && !strcmp(got.names[2], "hf.co/a+b"));
    for (unsigned i = 3u; i < JELLI_PREFS_MAX_MODELS; ++i)
        CHECK(got.names[i][0] == '\0');
    jelli_agent_models_offer(&good); /* The same list again is news too. */
    CHECK(jelli_agent_models_generation() == start + 2u);
    jelli_agent_models_forget();
    CHECK(jelli_agent_models_get(&got) == start + 3u && got.count == 0u && got.current == 0u);
    CHECK(!got.names[0][0]);
    jelli_agent_models_forget();
    CHECK(jelli_agent_models_generation() == start + 3u);
}

/* "loki models": the JSON the console answers, exact, and too_long rather than cut. */
static void shows(void)
{
    char out[CONSOLE_BODY + 8], expected[CONSOLE_BODY];
    online = false;
    current_case = 0u;
    jelli_agent_models_show(out, sizeof(out));
    CHECK(!strcmp(out, "{\"ok\":true,\"online\":false,\"current\":0,\"models\":[]}"));
    static char json[JELLI_LOKI_MAX_IN];
    size_t used = (size_t)snprintf(json, sizeof(json), "{\"t\":\"models\",\"seq\":1,\"models\":[");
    for (unsigned i = 0u; i < JELLI_LOKI_MODEL_MAX; ++i)
        used += (size_t)snprintf(json + used, sizeof(json) - used, "%s\"%02u%s\"", i ? "," : "", i,
                                 "-abcdefghij.abcdefghij:abcdefghij/+_xy");
    (void)snprintf(json + used, sizeof(json) - used, "],\"current\":15}");
    JelliLokiMessage biggest = decoded(json);
    CHECK(strlen(biggest.models[15]) == 40u);
    jelli_agent_models_offer(&biggest);
    online = true;
    used = (size_t)snprintf(expected, sizeof(expected),
                            "{\"ok\":true,\"online\":true,\"current\":15,\"models\":[");
    for (unsigned i = 0u; i < JELLI_LOKI_MODEL_MAX; ++i)
        used += (size_t)snprintf(expected + used, sizeof(expected) - used, "%s\"%s\"", i ? "," : "",
                                 biggest.models[i]);
    used += (size_t)snprintf(expected + used, sizeof(expected) - used, "]}");
    CHECK(used + 1u <= CONSOLE_BODY); /* The largest list fits the console's reply. */
    jelli_agent_models_show(out, sizeof(out));
    CHECK(!strcmp(out, expected));
    for (size_t capacity = 1u; capacity <= used + 1u; ++capacity) {
        current_case = (unsigned)capacity;
        memset(out, '#', sizeof(out));
        jelli_agent_models_show(out, capacity);
        CHECK(memchr(out, '\0', capacity) != NULL && out[capacity] == '#');
        if (capacity == used + 1u)
            CHECK(!strcmp(out, expected));
        else
            CHECK(!strncmp(out, "{\"ok\":false,\"error\":\"too_long\"}", capacity - 1u));
    }
    memset(out, '#', sizeof(out));
    jelli_agent_models_show(out, 0u);
    CHECK(out[0] == '#');
}

/* "loki model NAME": only a listed name, byte for byte, is sent; offline says so. */
static void picks(void)
{
    char out[CONSOLE_BODY];
    JelliLokiMessage list = decoded(
        "{\"t\":\"models\",\"seq\":1,\"models\":[\"Claude\",\"gpt-oss:20b\"],\"current\":0}");
    jelli_agent_models_offer(&list);
    static const char *const UNLISTED[] = {"claude", "Claud",        "Claude ", "gpt-oss",
                                           "",       "gpt-oss:20b:", "a\"b",    "Claude\n"};
    sendable = true;
    for (current_case = 0u; current_case < sizeof(UNLISTED) / sizeof(UNLISTED[0]); ++current_case) {
        jelli_agent_models_pick(UNLISTED[current_case], out, sizeof(out));
        CHECK(!strcmp(out, "{\"ok\":false,\"error\":\"not_offered\"}") && sent == 0u);
    }
    jelli_agent_models_pick("gpt-oss:20b", out, sizeof(out));
    CHECK(!strcmp(out, "{\"ok\":true}") && sent == 1u && !strcmp(sent_model, "gpt-oss:20b"));
    jelli_agent_models_pick("Claude", out, sizeof(out)); /* Already the default: sent anyway. */
    CHECK(!strcmp(out, "{\"ok\":true}") && sent == 2u && !strcmp(sent_model, "Claude"));
    sendable = false;
    jelli_agent_models_pick("Claude", out, sizeof(out));
    CHECK(!strcmp(out, "{\"ok\":false,\"error\":\"offline\"}") && sent == 2u);
    jelli_agent_models_forget(); /* The session ended: nothing is offered. */
    sendable = true;
    jelli_agent_models_pick("Claude", out, sizeof(out));
    CHECK(!strcmp(out, "{\"ok\":false,\"error\":\"not_offered\"}") && sent == 2u);
}

/* List k: 1 + k % 16 names "k-i", current k % count, the rest zero. */
static void list_numbered(unsigned k, JelliLokiMessage *message)
{
    memset(message, 0, sizeof(*message));
    message->type = JELLI_LOKI_MODELS;
    message->model_count = 1u + k % JELLI_LOKI_MODEL_MAX;
    message->current = k % message->model_count;
    for (unsigned i = 0u; i < message->model_count; ++i)
        (void)snprintf(message->models[i], sizeof(message->models[i]), "%u-%u", k, i);
}

static atomic_bool racing;

typedef struct {
    unsigned base, offers; /* Lists base, base + 2, ...; how many it offered. */
} Writer;

static void *writer(void *arg)
{
    static _Thread_local JelliLokiMessage message;
    Writer *self = arg;
    for (unsigned k = self->base; atomic_load(&racing); k += 2u) {
        list_numbered(k % 100000u, &message);
        jelli_agent_models_offer(&message);
        ++self->offers;
        if (k % 64u == self->base)
            jelli_agent_models_forget();
    }
    return NULL;
}

/* Every copy is one whole list (or none), and generations never go back. */
static void *reader(void *arg)
{
    (void)arg;
    JelliPrefsModels got;
    static _Thread_local JelliLokiMessage want;
    uint32_t last = 0u;
    for (unsigned n = 0u; n < RACE; ++n) {
        uint32_t generation = jelli_agent_models_get(&got);
        CHECK(generation >= last);
        last = generation;
        if (!got.count) {
            CHECK(!got.current && !got.names[0][0]);
            continue;
        }
        unsigned k = (unsigned)strtoul(got.names[0], NULL, 10);
        list_numbered(k, &want);
        CHECK(got.count == want.model_count && got.current == want.current);
        CHECK(!memcmp(got.names, want.models, sizeof(got.names)));
    }
    return NULL;
}

static void races(void)
{
    pthread_t writers[2], readers[2];
    Writer bases[2] = {{0u, 0u}, {1u, 0u}};
    current_case = 0u;
    atomic_store(&racing, true);
    for (unsigned i = 0u; i < 2u; ++i)
        CHECK(pthread_create(&writers[i], NULL, writer, &bases[i]) == 0);
    for (unsigned i = 0u; i < 2u; ++i)
        CHECK(pthread_create(&readers[i], NULL, reader, NULL) == 0);
    for (unsigned i = 0u; i < 2u; ++i)
        CHECK(pthread_join(readers[i], NULL) == 0);
    atomic_store(&racing, false);
    for (unsigned i = 0u; i < 2u; ++i)
        CHECK(pthread_join(writers[i], NULL) == 0 && bases[i].offers > 0u);
}

int main(void)
{
    keeps_and_refuses();
    shows();
    picks();
    races();
    printf("Firmware models slot checks passed (%d reads raced)\n", 2 * RACE);
    return 0;
}

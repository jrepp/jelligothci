/* White-box tests of loki/1's list kind (core/agent/loki.c read_list, loki_canonical.c
 * emit_list), which `ask.options` and `models` share since ADR-018: options keep their
 * rules (two to four lines of at most 40 bytes, approve exactly two, the card digest),
 * and models keep theirs (1 to 16 names of 1 to 40 characters of A-Z a-z 0-9 . _ : / +
 * -, current below the count), with neither list's item rule leaking into the other.
 * Every input is decoded from an exact-size heap copy, so a read past it is caught by
 * the address sanitizer. */
#include "jelli/agent_loki.h"
#include "jelli/agent_sha256.h"
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
static const char ASK_HEAD[] = "{\"t\":\"ask\",\"seq\":1,\"id\":\"a1\",\"nonce\":"
                               "\"00112233445566778899aabbccddeeff\",\"title\":\"T\",";
static unsigned current_case;
static JelliLokiMessage message;
static uint32_t rng = 0x2545f491u;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static JelliLokiResult decode(const char *json)
{
    size_t size = strlen(json);
    char *copy = malloc(size ? size : 1u);
    CHECK(copy != NULL);
    memcpy(copy, json, size);
    JelliLokiResult result = jelli_loki_decode(copy, size, &message);
    free(copy);
    return result;
}

/* {"t":"models","seq":1,"models":[<items>],"current":<current>}, items already JSON. */
static JelliLokiResult decode_models(const char *items, unsigned current)
{
    static char json[TEXT];
    (void)snprintf(json, sizeof(json),
                   "{\"t\":\"models\",\"seq\":1,\"models\":[%s],\"current\":%u}", items, current);
    return decode(json);
}

/* An ask of kind with the options given as JSON items. */
static JelliLokiResult decode_ask(const char *kind, const char *items)
{
    static char json[TEXT];
    (void)snprintf(json, sizeof(json), "%s\"kind\":\"%s\",\"options\":[%s],\"timeout_ms\":60000}",
                   ASK_HEAD, kind, items);
    return decode(json);
}

/* count items "x0","x1",... (or names of length bytes of fill, when fill is set). */
static void items_of(char *out, size_t capacity, unsigned count, size_t length, char fill)
{
    size_t used = 0u;
    out[0] = '\0';
    for (unsigned i = 0u; i < count; ++i) {
        CHECK(used + length + 16u < capacity);
        used += (size_t)snprintf(out + used, capacity - used, "%s\"", i ? "," : "");
        if (fill) {
            memset(out + used, fill, length);
            used += length;
        } else {
            used += (size_t)snprintf(out + used, capacity - used, "x%u", i);
        }
        out[used++] = '"';
        out[used] = '\0';
    }
}

/* choose takes two to four options and approve exactly two; models are not touched. */
static void option_counts(void)
{
    char items[512];
    for (current_case = 0u; current_case <= 6u; ++current_case) {
        items_of(items, sizeof(items), current_case, 0u, '\0');
        bool choose = current_case >= 2u && current_case <= 4u, approve = current_case == 2u;
        CHECK(decode_ask("choose", items) == (choose ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
        if (choose)
            CHECK(message.option_count == current_case && !message.model_count &&
                  !strcmp(message.options[current_case - 1u],
                          current_case == 2u ? "x1" : (current_case == 3u ? "x2" : "x3")));
        CHECK(decode_ask("approve", items) == (approve ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
    }
}

/* Options count bytes, not characters, and may hold what a model name may not. */
static void option_bytes(void)
{
    static const struct {
        const char *json, *decoded;
    } FREE[] = {{"\"gpt oss\"", "gpt oss"},
                {"\"a\\\"b\"", "a\"b"},
                {"\"a\\\\b\"", "a\\b"},
                {"\"\\u00e9\"", "\xc3\xa9"},
                {"\"\"", ""},
                {"\"a,b\"", "a,b"},
                {"\"\xf0\x9f\xa6\x84\"", "\xf0\x9f\xa6\x84"},
                {"\"*~?\"", "*~?"}};
    char items[512];
    current_case = 0u;
    for (size_t i = 0u; i < sizeof(FREE) / sizeof(FREE[0]); ++i) {
        (void)snprintf(items, sizeof(items), "\"x\",%s", FREE[i].json);
        CHECK(decode_ask("choose", items) == JELLI_LOKI_OK);
        CHECK(!strcmp(message.options[1], FREE[i].decoded));
        (void)snprintf(items, sizeof(items), "%s", FREE[i].json);
        CHECK(decode_models(items, 0u) == JELLI_LOKI_INVALID); /* Not a model name. */
    }
    for (size_t length = 38u; length <= 41u; ++length) {
        current_case = (unsigned)length;
        items_of(items, sizeof(items), 2u, length, 'o');
        CHECK(decode_ask("choose", items) == (length <= 40u ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
        /* length - 1 ASCII bytes and a two-byte é: length + 1 bytes in all. */
        (void)snprintf(items, sizeof(items), "\"a\",\"%.*s\xc3\xa9\"", (int)(length - 1u),
                       "oooooooooooooooooooooooooooooooooooooooooooo");
        CHECK(decode_ask("choose", items) == (length < 40u ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
    }
    static const char *const BAD[] = {"\"a\\nb\"", "\"a\\u000ab\"", "\"\\t\"", "\"a\\u001f\"",
                                      "1",         "null",          "[\"a\"]", "{}"};
    for (size_t i = 0u; i < sizeof(BAD) / sizeof(BAD[0]); ++i) {
        current_case = (unsigned)i;
        (void)snprintf(items, sizeof(items), "\"a\",%s", BAD[i]);
        CHECK(decode_ask("choose", items) == JELLI_LOKI_INVALID);
    }
}

/* The card digest is the SHA-256 of the canonical form from "kind=" to " timeout_ms=":
 * checked for seeded option lists, escapes and all, with the hash taken here. */
static void card_digests(void)
{
    static const char *const PIECES[] = {"Allow",   "Deny", "a b", "\\\"q\\\"", "\\\\",
                                         "\\u00e9", "",     "\\/", "o",         "\\ud83e\\udd84"};
    static char canonical[TEXT];
    char items[512], digest[JELLI_LOKI_DIGEST], expected[JELLI_LOKI_DIGEST];
    for (current_case = 0u; current_case < 3000u; ++current_case) {
        unsigned count = 2u + next() % 3u;
        size_t used = 0u;
        for (unsigned i = 0u; i < count; ++i)
            used += (size_t)snprintf(items + used, sizeof(items) - used, "%s\"%s%s\"", i ? "," : "",
                                     PIECES[next() % 10u], PIECES[next() % 10u]);
        CHECK(decode_ask(count == 2u && next() % 2u ? "approve" : "choose", items) ==
              JELLI_LOKI_OK);
        CHECK(jelli_loki_canonical(&message, canonical, sizeof(canonical)) > 0u);
        const char *card = strstr(canonical, "kind="), *end = strstr(canonical, " timeout_ms=");
        CHECK(card && end && end > card);
        JelliSha256 sha;
        uint8_t hash[JELLI_SHA256_SIZE];
        jelli_sha256_init(&sha);
        jelli_sha256_update(&sha, card, (size_t)(end - card));
        jelli_sha256_final(&sha, hash);
        for (size_t i = 0u; i < JELLI_SHA256_SIZE; ++i)
            (void)snprintf(expected + 2u * i, 3u, "%02x", (unsigned)hash[i]);
        jelli_loki_card_digest(&message, digest);
        CHECK(!strcmp(digest, expected));
    }
    /* docs/loki-protocol.md's example: ask-approve's card. */
    CHECK(decode("{\"t\":\"ask\",\"seq\":10,\"id\":\"a1\",\"nonce\":"
                 "\"00112233445566778899aabbccddeeff\","
                 "\"kind\":\"approve\",\"title\":\"Run outside the sandbox?\",\"detail\":\"npm run "
                 "dev\\nin codergotchi\",\"options\":[\"Allow\",\"Deny\"],\"timeout_ms\":60000}") ==
          JELLI_LOKI_OK);
    jelli_loki_card_digest(&message, digest);
    CHECK(!strcmp(digest, "d9f7edeca33a2b6eb1b3d520c54f673eecfa2aa925af95b16333380873ff7500"));
}

/* Every count 0..17 against every current 0..count: 1..16 names and current below. */
static void model_counts(void)
{
    char items[512];
    for (unsigned count = 0u; count <= 17u; ++count) {
        items_of(items, sizeof(items), count, 0u, '\0');
        for (unsigned current = 0u; current <= count + 1u; ++current) {
            current_case = count * 100u + current;
            bool ok = count >= 1u && count <= 16u && current < count;
            CHECK(decode_models(items, current) == (ok ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
            if (!ok)
                continue;
            CHECK(message.type == JELLI_LOKI_MODELS && message.model_count == count &&
                  message.current == current && !message.option_count);
            char last[8];
            (void)snprintf(last, sizeof(last), "x%u", count - 1u);
            CHECK(!strcmp(message.models[count - 1u], last));
        }
    }
    /* current is a number in range, never a string, fraction, or the 33rd bit. */
    static const char *const CURRENTS[] = {"\"0\"", "0.0",        "1e0", "-0",   "-1",
                                           "16",    "4294967296", "00",  "true", "null"};
    char json[256];
    for (size_t i = 0u; i < sizeof(CURRENTS) / sizeof(CURRENTS[0]); ++i) {
        current_case = (unsigned)i;
        (void)snprintf(json, sizeof(json),
                       "{\"t\":\"models\",\"seq\":1,\"models\":[\"a\"],\"current\":%s}",
                       CURRENTS[i]);
        CHECK(decode(json) == JELLI_LOKI_INVALID);
    }
}

/* Names of 1..40 characters; 0 and 41 refused, also when escapes spell them. */
static void model_lengths(void)
{
    char items[1024];
    for (size_t length = 0u; length <= 41u; ++length) {
        current_case = (unsigned)length;
        bool ok = length >= 1u && length <= 40u;
        items_of(items, sizeof(items), 1u, length, 'm');
        CHECK(decode_models(items, 0u) == (ok ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
        CHECK(!ok || strlen(message.models[0]) == length);
        size_t used = 0u; /* Each character as \u006d: six bytes in, one out. */
        items[used++] = '"';
        for (size_t i = 0u; i < length; ++i)
            used += (size_t)snprintf(items + used, sizeof(items) - used, "\\u006d");
        items[used++] = '"';
        items[used] = '\0';
        CHECK(decode_models(items, 0u) == (ok ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
        CHECK(!ok || strlen(message.models[0]) == length);
    }
}

/* One character in the middle of a name, as raw text and as a \u escape: allowed
 * exactly when it is one of the 68 the protocol lists. */
static void model_characters(void)
{
    char items[64];
    for (unsigned c = 1u; c <= 0xffu; ++c) {
        current_case = c;
        bool allowed = c < 0x80u && strchr(ALLOWED, (int)c) != NULL;
        (void)snprintf(items, sizeof(items), "\"a\\u%04xb\"", c);
        CHECK(decode_models(items, 0u) == (allowed ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
        if (allowed)
            CHECK(message.models[0][1] == (char)c && strlen(message.models[0]) == 3u);
        if (c < 0x20u || c == '"' || c == '\\' || c >= 0x80u)
            continue; /* Not valid JSON raw (or UTF-8 as a lone byte). */
        (void)snprintf(items, sizeof(items), "\"a%cb\"", (char)c);
        CHECK(decode_models(items, 0u) == (allowed ? JELLI_LOKI_OK : JELLI_LOKI_INVALID));
    }
    CHECK(sizeof(ALLOWED) - 1u == 68u);
    /* Escapes that decode to allowed characters, and the canonical form shows them plain. */
    char canonical[256];
    CHECK(decode_models("\"hf.co\\/a\\u002eb\\u003A\\u002B\"", 0u) == JELLI_LOKI_OK);
    CHECK(!strcmp(message.models[0], "hf.co/a.b:+"));
    CHECK(jelli_loki_canonical(&message, canonical, sizeof(canonical)) > 0u);
    CHECK(!strcmp(canonical, "models seq=1 models=[\"hf.co/a.b:+\"] current=0"));
    static const char *const BAD[] = {
        "\"\\u0000\"", "\"a\\u0020\"", "\"\\\"\"",  "\"\\\\\"",        "\"\\ud83e\\udd84\"",
        "\"\\ud83e\"", "\"\xc3\xa9\"", "\"a\\tb\"", "\"\xef\xbc\xa1\""}; /* Fullwidth A. */
    for (size_t i = 0u; i < sizeof(BAD) / sizeof(BAD[0]); ++i) {
        current_case = (unsigned)i;
        CHECK(decode_models(BAD[i], 0u) == JELLI_LOKI_INVALID);
    }
}

/* The shape around the list: fields read once, unknown ones skipped, the other list's
 * key unknown to each type, whitespace allowed, and nothing but an array of strings. */
static void model_shapes(void)
{
    static const struct {
        const char *json;
        JelliLokiResult result;
    } CASES[] = {
        {"{\"t\":\"models\",\"seq\":1,\"current\":0,\"models\":[\"a\"]}", JELLI_LOKI_OK},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[ \"a\" , \"b\" ],\"current\":1}", JELLI_LOKI_OK},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[\"a\",\"a\"],\"current\":1}", JELLI_LOKI_OK},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[\"a\"],\"current\":0,\"options\":[\"a b\"]}",
         JELLI_LOKI_OK},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[\"a\"],\"current\":0,\"x\":{\"models\":[1]}}",
         JELLI_LOKI_OK},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[\"a\"],\"models\":[\"b\"],\"current\":0}",
         JELLI_LOKI_INVALID},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[\"a\"],\"current\":0,\"current\":0}",
         JELLI_LOKI_INVALID},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[\"a\",],\"current\":0}", JELLI_LOKI_INVALID},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[,\"a\"],\"current\":0}", JELLI_LOKI_INVALID},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[\"a\" \"b\"],\"current\":0}", JELLI_LOKI_INVALID},
        {"{\"t\":\"models\",\"seq\":1,\"models\":null,\"current\":0}", JELLI_LOKI_INVALID},
        {"{\"t\":\"models\",\"seq\":1,\"models\":{\"a\":1},\"current\":0}", JELLI_LOKI_INVALID},
        {"{\"t\":\"models\",\"seq\":1,\"models\":[\"a\"],\"current\":0", JELLI_LOKI_INVALID},
        {"{\"t\":\"models\",\"models\":[\"a\"],\"current\":0}", JELLI_LOKI_INVALID},
    };
    for (current_case = 0u; current_case < sizeof(CASES) / sizeof(CASES[0]); ++current_case)
        CHECK(decode(CASES[current_case].json) == CASES[current_case].result);
    /* An ask skips a models key, however its names look, and keeps its own options. */
    CHECK(
        decode(
            "{\"t\":\"ask\",\"seq\":1,\"id\":\"a1\",\"nonce\":\"00112233445566778899aabbccddeeff\","
            "\"kind\":\"choose\",\"title\":\"T\",\"models\":[\"a b\",\"\"],\"current\":9,"
            "\"options\":[\"x\",\"y\"],\"timeout_ms\":1000}") == JELLI_LOKI_OK);
    CHECK(message.type == JELLI_LOKI_ASK && message.option_count == 2u && !message.model_count &&
          !message.current && !message.models[0][0]);
    /* A models message after an ask carries none of the ask's options. */
    CHECK(decode_models("\"a\",\"b\"", 1u) == JELLI_LOKI_OK);
    CHECK(!message.option_count && !message.options[0][0] && !message.title[0]);
}

/* The largest list: 16 names of 40 fits the 2048 bytes of a message; as escapes it
 * does not, and is refused rather than cut. */
static void largest_lists(void)
{
    static char items[TEXT * 2u];
    current_case = 0u;
    items_of(items, sizeof(items), 16u, 40u, 'Z');
    CHECK(decode_models(items, 15u) == JELLI_LOKI_OK && message.model_count == 16u);
    static char canonical[TEXT];
    size_t size = jelli_loki_canonical(&message, canonical, sizeof(canonical));
    CHECK(size == strlen("models seq=1 models=[] current=15") + (size_t)16u * 42u + 15u);
    CHECK(jelli_loki_canonical(&message, canonical, size) == 0u); /* No room for its NUL. */
    CHECK(jelli_loki_canonical(&message, canonical, size + 1u) == size);
    size_t used = 0u;
    for (unsigned i = 0u; i < 16u; ++i) {
        used += (size_t)snprintf(items + used, sizeof(items) - used, "%s\"", i ? "," : "");
        for (unsigned c = 0u; c < 40u; ++c)
            used += (size_t)snprintf(items + used, sizeof(items) - used, "\\u005a");
        used += (size_t)snprintf(items + used, sizeof(items) - used, "\"");
    }
    CHECK(used > JELLI_LOKI_MAX_IN);
    CHECK(decode_models(items, 0u) == JELLI_LOKI_INVALID);
}

/* A list one past its capacity is refused before its extra item is written: bytes
 * after the message stay as they were, so the decoder never writes past the last item. */
static void no_item_past_the_list(void)
{
    typedef struct {
        JelliLokiMessage message;
        unsigned char after[64];
    } Guarded;
    static char items[1024];
    Guarded *guarded = malloc(sizeof(Guarded));
    CHECK(guarded != NULL);
    for (current_case = 0u; current_case < 2u; ++current_case) {
        memset(guarded, 0xA5, sizeof(*guarded));
        bool models = current_case == 0u;
        items_of(items, sizeof(items), models ? 17u : 5u, 40u, 'q');
        static char json[TEXT];
        if (models)
            (void)snprintf(json, sizeof(json),
                           "{\"t\":\"models\",\"seq\":1,\"models\":[%s],\"current\":0}", items);
        else
            (void)snprintf(json, sizeof(json),
                           "%s\"kind\":\"choose\",\"options\":[%s],\"timeout_ms\":60000}", ASK_HEAD,
                           items);
        CHECK(jelli_loki_decode(json, strlen(json), &guarded->message) == JELLI_LOKI_INVALID);
        for (size_t i = 0u; i < sizeof(guarded->after); ++i)
            CHECK(guarded->after[i] == 0xA5u);
        unsigned count = models ? guarded->message.model_count : guarded->message.option_count;
        CHECK(count <= (models ? JELLI_LOKI_MODEL_MAX : JELLI_LOKI_OPTIONS));
    }
    free(guarded);
}

int main(void)
{
    option_counts();
    option_bytes();
    card_digests();
    model_counts();
    model_lengths();
    model_characters();
    model_shapes();
    largest_lists();
    no_item_past_the_list();
    printf("Loki list checks passed (options and models)\n");
    return 0;
}

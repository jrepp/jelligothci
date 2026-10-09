/* Property, model, and metamorphic tests for the agent console byte parser.
 * Every generated stream uses a fixed seed so a failure prints a replayable case. */
#include "jelli/agent_console.h"
#include <inttypes.h>
#include <stdint.h>
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

/* Reports the generator seed and line so the failing input can be replayed. */
#define CHECK_CASE(expr, seed, line)                                                               \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed %u, line %u)\n", __FILE__, __LINE__, #expr,           \
                    (unsigned)(seed), (unsigned)(line));                                           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SEEDS = 3000u, STREAM = 2048u, RAW = 600u };

/* What a simple reference parser decides about one line (its bytes before the LF). */
typedef struct {
    bool accepted;
    uint32_t id;
    unsigned count;
    char words[JELLI_AGENT_WORDS][JELLI_AGENT_LINE];
} Expected;

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t below(uint32_t *state, uint32_t bound) { return next_random(state) % bound; }

/* Digits only, no sign, value at most UINT32_MAX, computed in 64 bits. */
static bool reference_number(const char *text, size_t size, uint32_t *out)
{
    uint64_t value = 0u;
    if (!size)
        return false;
    for (size_t i = 0u; i < size; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return false;
        value = value * 10u + (uint64_t)(text[i] - '0');
        if (value > UINT32_MAX)
            return false;
    }
    *out = (uint32_t)value;
    return true;
}

/* Copies the next space-separated token of kept[*at..size) into word; returns its size. */
static size_t next_token(const char *kept, size_t size, size_t *at, char *word)
{
    while (*at < size && kept[*at] == ' ')
        ++*at;
    size_t length = 0u;
    while (*at < size && kept[*at] != ' ')
        word[length++] = kept[(*at)++];
    word[length] = '\0';
    return length;
}

/* CRs vanish; any other byte outside printable ASCII, or more than LINE - 1 kept bytes,
 * rejects. Then: "@A1", a decimal id, and 1..WORDS words. */
static void reference(const unsigned char *bytes, size_t size, Expected *expected)
{
    char kept[JELLI_AGENT_LINE] = {0};
    size_t used = 0u;
    memset(expected, 0, sizeof(*expected));
    for (size_t i = 0u; i < size; ++i) {
        if (bytes[i] == '\r')
            continue;
        if (bytes[i] < 0x20u || bytes[i] > 0x7eu || used == JELLI_AGENT_LINE - 1u)
            return;
        kept[used++] = (char)bytes[i];
    }
    char token[JELLI_AGENT_LINE];
    size_t at = 0u;
    if (next_token(kept, used, &at, token) != 3u || strcmp(token, "@A1") != 0)
        return;
    size_t length = next_token(kept, used, &at, token);
    if (!reference_number(token, length, &expected->id))
        return;
    while (next_token(kept, used, &at, token)) {
        if (expected->count == JELLI_AGENT_WORDS)
            return;
        memcpy(expected->words[expected->count++], token, strlen(token) + 1u);
    }
    expected->accepted = expected->count > 0u;
}

/* Every word lies inside the console's line, is non-empty, printable, space-free, and
 * NUL-terminated before the end of that buffer. */
static bool words_are_sound(const JelliAgentConsole *console, const JelliAgentRequest *request)
{
    if (request->count < 1u || request->count > JELLI_AGENT_WORDS)
        return false;
    const char *end = console->line + sizeof(console->line);
    for (unsigned n = 0u; n < request->count; ++n) {
        const char *word = request->words[n];
        if (word < console->line || word >= end || !memchr(word, '\0', (size_t)(end - word)))
            return false;
        if (!*word)
            return false;
        for (const char *c = word; *c; ++c)
            if (*c <= ' ' || *c > '~')
                return false;
    }
    return true;
}

static bool matches(const JelliAgentRequest *request, const Expected *expected)
{
    if (request->id != expected->id || request->count != expected->count)
        return false;
    for (unsigned n = 0u; n < request->count; ++n)
        if (strcmp(request->words[n], expected->words[n]) != 0)
            return false;
    return true;
}

/* Appends a token that is usually valid and sometimes subtly wrong. */
static size_t token(uint32_t *state, unsigned char *out, unsigned kind)
{
    static const char *const tags[] = {"@A1", "@A1", "@A1", "@A1", "@A1", "@J1", "@a1", "@A12"};
    static const char *const ids[] = {
        "0",          "7",  "4294967295", "00042",       "123",
        "4294967296", "-1", "+3",         "99999999999", "18446744073709551616"};
    /* Half the time the tag or id is drawn only from the valid choices. */
    bool valid = below(state, 2u) != 0u;
    const char *text = kind == 0u ? tags[below(state, valid ? 5u : 8u)] : NULL;
    if (kind == 1u)
        text = ids[below(state, valid ? 5u : 10u)];
    if (text) {
        memcpy(out, text, strlen(text));
        return strlen(text);
    }
    size_t length = 1u + below(state, below(state, 16u) == 0u ? 300u : 12u);
    for (size_t i = 0u; i < length; ++i)
        out[i] = (unsigned char)('!' + below(state, 94u));
    return length;
}

/* A line shaped like a request: tag, id, words, with random spacing, CRs, and an
 * occasional byte from the full 0..255 range. Returns its size, LF excluded. */
static size_t shaped_line(uint32_t *state, unsigned char *out, size_t room)
{
    size_t used = 0u;
    unsigned tokens = below(state, 5u) ? 3u + below(state, JELLI_AGENT_WORDS) : below(state, 13u);
    for (unsigned t = 0u; t < tokens && used + 340u < room; ++t) {
        /* Usually one separator; sometimes several, and rarely none so tokens merge. */
        unsigned spaces = below(state, 4u) == 0u ? below(state, 4u) : 1u;
        if (t && !spaces && below(state, 4u) != 0u)
            spaces = 1u;
        for (unsigned s = 0u; s < spaces; ++s)
            out[used++] = below(state, 6u) == 0u ? '\r' : ' ';
        used += token(state, out + used, t < 2u ? t : 2u);
    }
    if (used && below(state, 10u) == 0u)
        out[below(state, (uint32_t)used)] = (unsigned char)below(state, 256u);
    while (used + 1u < room && below(state, 5u) == 0u)
        out[used++] = below(state, 2u) ? ' ' : '\r';
    return used;
}

/* Arbitrary bytes from all 256 values, LF included, so lines have random lengths. */
static size_t raw_bytes(uint32_t *state, unsigned char *out)
{
    size_t size = below(state, RAW);
    for (size_t i = 0u; i < size; ++i)
        out[i] = (unsigned char)below(state, 256u);
    return size;
}

/* Feeds one stream and checks every LF against the reference; returns acceptances. */
static unsigned check_stream(uint32_t seed, const unsigned char *bytes, size_t size)
{
    JelliAgentConsole console = {0};
    JelliAgentRequest request;
    Expected expected;
    size_t start = 0u;
    unsigned lines = 0u, accepted = 0u;
    for (size_t i = 0u; i < size; ++i) {
        memset(&request, 0xa5, sizeof(request));
        bool ready = jelli_agent_console_feed(&console, (char)bytes[i], &request);
        if (bytes[i] != '\n') {
            CHECK_CASE(!ready, seed, lines);
            continue;
        }
        reference(bytes + start, i - start, &expected);
        CHECK_CASE(ready == expected.accepted, seed, lines);
        if (ready) {
            CHECK_CASE(words_are_sound(&console, &request), seed, lines);
            CHECK_CASE(matches(&request, &expected), seed, lines);
            ++accepted;
        }
        CHECK_CASE(console.used == 0u && !console.discard, seed, lines);
        start = i + 1u;
        ++lines;
    }
    /* Whatever came before, one LF resynchronizes and a valid request then parses. */
    (void)jelli_agent_console_feed(&console, '\n', &request);
    const char *probe = "@A1 3141592653 ok\n";
    unsigned ready = 0u;
    for (const char *p = probe; *p; ++p)
        ready += jelli_agent_console_feed(&console, *p, &request) ? 1u : 0u;
    CHECK_CASE(ready == 1u && request.id == 3141592653u && request.count == 1u, seed, lines);
    CHECK_CASE(!strcmp(request.words[0], "ok"), seed, lines);
    CHECK_CASE(accepted <= lines, seed, lines);
    return accepted;
}

static void random_streams_match_the_reference(void)
{
    static unsigned char bytes[STREAM];
    unsigned accepted = 0u, rejected_lines = 0u;
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 2654435761u;
        size_t size = 0u;
        if (seed % 4u == 0u) {
            size = raw_bytes(&state, bytes);
        } else {
            while (size + 400u < sizeof(bytes) && below(&state, 6u) != 0u) {
                size += shaped_line(&state, bytes + size, sizeof(bytes) - size - 1u);
                bytes[size++] = '\n';
            }
        }
        unsigned lfs = 0u;
        for (size_t i = 0u; i < size; ++i)
            lfs += bytes[i] == '\n' ? 1u : 0u;
        unsigned ok = check_stream(seed, bytes, size);
        CHECK_CASE(ok <= lfs, seed, lfs);
        accepted += ok;
        rejected_lines += lfs - ok;
    }
    /* The generator must reach both outcomes often, or the comparison proves little. */
    CHECK(accepted > SEEDS && rejected_lines > SEEDS);
}

/* Feeding the same stream twice gives the same requests: no hidden state survives. */
static void parsing_is_deterministic(void)
{
    static const char stream[] = "@A1 1 a\r\n\x80@A1 2 b\n@A1  3  c d \n@A1 4\n@A1 5 e\n";
    JelliAgentConsole first = {0}, second = {0};
    JelliAgentRequest a, b;
    for (size_t i = 0u; i + 1u < sizeof(stream); ++i) {
        bool ready_a = jelli_agent_console_feed(&first, stream[i], &a);
        bool ready_b = jelli_agent_console_feed(&second, stream[i], &b);
        CHECK(ready_a == ready_b);
        CHECK(!ready_a || (a.id == b.id && a.count == b.count && !strcmp(a.words[0], b.words[0])));
    }
    CHECK(!memcmp(first.line, second.line, sizeof(first.line)) && first.used == second.used);
}

/* Every capacity from 0 to len + 2: the reply fits exactly when capacity > len, and no
 * byte at or past the capacity is ever written. */
static void replies_respect_every_capacity(void)
{
    static const char *const bodies[] = {"", "{}", "{\"ok\":true,\"app\":\"agent\"}"};
    static const uint32_t ids[] = {0u, 9u, 4294967295u};
    for (size_t b = 0u; b < sizeof(bodies) / sizeof(bodies[0]); ++b) {
        for (size_t k = 0u; k < sizeof(ids) / sizeof(ids[0]); ++k) {
            char expected[64], out[64];
            int length =
                snprintf(expected, sizeof(expected), "\n@A1 %" PRIu32 " %s\n", ids[k], bodies[b]);
            CHECK(length > 0 && (size_t)length + 3u < sizeof(out));
            for (size_t capacity = 0u; capacity <= (size_t)length + 2u; ++capacity) {
                memset(out, 0x5a, sizeof(out));
                size_t size = jelli_agent_console_reply(out, capacity, ids[k], bodies[b]);
                if (capacity > (size_t)length) {
                    CHECK(size == (size_t)length && !memcmp(out, expected, size + 1u));
                } else {
                    CHECK(size == 0u && out[0] == (capacity ? '\0' : 0x5a));
                }
                for (size_t i = capacity; i < sizeof(out); ++i)
                    CHECK(out[i] == 0x5a);
            }
        }
    }
}

/* Metamorphic: a reply whose body is words joined by spaces reads back as one request
 * with the same id and words, so the two directions of the framing agree. */
static void replies_read_back_as_requests(void)
{
    for (uint32_t seed = 1u; seed <= 2000u; ++seed) {
        uint32_t state = seed * 40503u + 1u;
        char words[JELLI_AGENT_WORDS][24], body[JELLI_AGENT_WORDS * 24u], out[JELLI_AGENT_LINE];
        unsigned count = 1u + below(&state, JELLI_AGENT_WORDS);
        size_t used = 0u;
        for (unsigned n = 0u; n < count; ++n) {
            size_t length = 1u + below(&state, 20u);
            for (size_t i = 0u; i < length; ++i)
                words[n][i] = (char)('!' + below(&state, 94u));
            words[n][length] = '\0';
            if (n)
                body[used++] = ' ';
            memcpy(body + used, words[n], length + 1u);
            used += length;
        }
        uint32_t id = next_random(&state);
        size_t size = jelli_agent_console_reply(out, sizeof(out), id, body);
        CHECK_CASE(size > 0u, seed, 0u);
        JelliAgentConsole console = {0};
        JelliAgentRequest request;
        unsigned ready = 0u;
        for (size_t i = 0u; i < size; ++i)
            ready += jelli_agent_console_feed(&console, out[i], &request) ? 1u : 0u;
        CHECK_CASE(ready == 1u && request.id == id && request.count == count, seed, 0u);
        for (unsigned n = 0u; n < count; ++n)
            CHECK_CASE(!strcmp(request.words[n], words[n]), seed, n);
    }
}

/* The strict decimal agrees with a 64-bit reference around UINT32_MAX and on random
 * short strings over digits, signs, spaces, and letters. */
static void numbers_match_the_reference(void)
{
    char text[32];
    for (uint64_t value = UINT32_MAX - 3000u; value <= (uint64_t)UINT32_MAX + 3000u; ++value) {
        (void)snprintf(text, sizeof(text), "%" PRIu64, value);
        uint32_t got = 0u;
        CHECK(jelli_agent_number(text, &got) == (value <= UINT32_MAX));
        CHECK(value > UINT32_MAX || got == (uint32_t)value);
    }
    static const char alphabet[] = "0123456789000 -+x";
    for (uint32_t seed = 1u; seed <= 20000u; ++seed) {
        uint32_t state = seed * 747796405u + 1u;
        size_t length = below(&state, 14u);
        for (size_t i = 0u; i < length; ++i)
            text[i] = alphabet[below(&state, sizeof(alphabet) - 1u)];
        text[length] = '\0';
        uint32_t got = 7u, want = 7u;
        bool expected = reference_number(text, length, &want);
        CHECK_CASE(jelli_agent_number(text, &got) == expected, seed, 0u);
        CHECK_CASE(got == want, seed, 0u); /* Unchanged on rejection, equal on success. */
    }
}

int main(void)
{
    random_streams_match_the_reference();
    parsing_is_deterministic();
    replies_respect_every_capacity();
    replies_read_back_as_requests();
    numbers_match_the_reference();
    puts("Agent console fuzz checks passed");
    return 0;
}

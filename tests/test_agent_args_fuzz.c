/* Property, reference-model, and metamorphic tests for the agent argument parsers and
 * the microphone frame header. Every case uses a fixed seed so a failure is replayable. */
#include "jelli/agent_args.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reports the generator seed and the input text so the case can be replayed and pinned. */
#define CHECK_CASE(expr, seed, text)                                                               \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed %u, input \"%s\")\n", __FILE__, __LINE__, #expr,      \
                    (unsigned)(seed), (text));                                                     \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SEEDS = 20000u, TEXT = 64u, CAPACITY = 24u, GUARD = 8u, SENTINEL = 0xa5u, PART = 8u };

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

static uint32_t seed_state(uint32_t seed) { return seed * 2654435761u + 0x9e3779b9u; }

/* Independent of hex_digit: a lookup in the digit alphabet. */
static int reference_nibble(unsigned char c)
{
    static const char digits[] = "0123456789abcdef";
    unsigned char lower = (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32u) : c;
    const char *at = lower ? strchr(digits, lower) : NULL;
    return at ? (int)(at - digits) : -1;
}

/* Returns the decoded size, or -1 when the word must be refused. */
static int reference_hex(const char *text, size_t capacity, uint8_t *out)
{
    size_t length = strlen(text);
    if (length % 2u != 0u || length / 2u > capacity)
        return -1;
    for (size_t i = 0u; i < length; i += 2u) {
        int high = reference_nibble((unsigned char)text[i]);
        int low = reference_nibble((unsigned char)text[i + 1u]);
        if (high < 0 || low < 0)
            return -1;
        out[i / 2u] = (uint8_t)(high << 4 | low);
    }
    return (int)(length / 2u);
}

/* Mostly hex digits of either case with an occasional byte from 1..255, so both
 * outcomes are common; a quarter of the cases are bytes from the full range, where a
 * zero ends the word early just as it does for the console. */
static void hex_text(uint32_t *state, char *text, size_t capacity)
{
    static const char alphabet[] = "0123456789abcdefABCDEF";
    size_t length = below(state, 2u * (uint32_t)capacity + 4u);
    bool raw = below(state, 4u) == 0u;
    for (size_t i = 0u; i < length; ++i) {
        if (raw)
            text[i] = (char)below(state, 256u);
        else if (below(state, 40u) == 0u)
            text[i] = (char)(1u + below(state, 255u));
        else
            text[i] = alphabet[below(state, sizeof(alphabet) - 1u)];
    }
    text[length] = '\0';
}

static unsigned fuzz_hex(void)
{
    unsigned accepted = 0u;
    for (uint32_t seed = 0u; seed < SEEDS; ++seed) {
        uint32_t state = seed_state(seed);
        char text[TEXT];
        uint8_t out[CAPACITY + GUARD], expected[CAPACITY];
        size_t capacity = below(&state, CAPACITY + 1u);
        hex_text(&state, text, capacity);
        size_t size = 0xdeadu;
        memset(out, SENTINEL, sizeof(out));
        int want = reference_hex(text, capacity, expected);
        bool ok = jelli_agent_hex_decode(text, out, capacity, &size);
        CHECK_CASE(ok == (want >= 0), seed, text);
        /* Nothing past the decoded size is written, and nothing at all on failure. */
        size_t written = ok ? size : 0u;
        CHECK_CASE(ok ? size == (size_t)want : size == 0xdeadu, seed, text);
        CHECK_CASE(!ok || memcmp(out, expected, written) == 0, seed, text);
        for (size_t i = written; i < sizeof(out); ++i)
            CHECK_CASE(out[i] == SENTINEL, seed, text);
        accepted += ok;
    }
    return accepted;
}

/* Metamorphic: encoding any bytes in any mix of cases and decoding gives them back,
 * for every capacity from the exact size up; one byte less is always refused. */
static void hex_round_trips(void)
{
    static const char lower[] = "0123456789abcdef", upper[] = "0123456789ABCDEF";
    for (uint32_t seed = 0u; seed < SEEDS / 4u; ++seed) {
        uint32_t state = seed_state(seed);
        uint8_t bytes[CAPACITY], out[CAPACITY + GUARD];
        char text[2u * CAPACITY + 1u];
        size_t count = below(&state, CAPACITY + 1u), size = 0u;
        for (size_t i = 0u; i < count; ++i) {
            bytes[i] = (uint8_t)below(&state, 256u);
            text[2u * i] = (below(&state, 2u) ? upper : lower)[bytes[i] >> 4];
            text[2u * i + 1u] = (below(&state, 2u) ? upper : lower)[bytes[i] & 15u];
        }
        text[2u * count] = '\0';
        for (size_t capacity = count; capacity <= CAPACITY; ++capacity) {
            memset(out, SENTINEL, sizeof(out));
            CHECK_CASE(jelli_agent_hex_decode(text, out, capacity, &size) && size == count, seed,
                       text);
            CHECK_CASE(memcmp(out, bytes, count) == 0 && out[count] == SENTINEL, seed, text);
        }
        if (count) {
            size = 0xdeadu;
            CHECK_CASE(!jelli_agent_hex_decode(text, out, count - 1u, &size) && size == 0xdeadu,
                       seed, text);
        }
    }
}

/* Splits on '.' by hand: exactly four parts of one to three ASCII digits, no leading
 * zero unless the part is "0", each at most 255. */
static bool reference_ipv4(const char *text, uint8_t address[4])
{
    unsigned part = 0u, digits = 0u, value = 0u;
    char first = '\0';
    for (const char *c = text;; ++c) {
        if (*c == '.' || *c == '\0') {
            if (!digits || (digits > 1u && first == '0') || value > 255u || part > 3u)
                return false;
            address[part++] = (uint8_t)value;
            if (*c == '\0')
                return part == 4u;
            digits = value = 0u;
        } else if (*c >= '0' && *c <= '9' && digits < 3u) {
            first = digits ? first : *c;
            value = value * 10u + (unsigned)(*c - '0');
            ++digits;
        } else {
            return false;
        }
    }
}

/* One part: usually a number in 0..299 (sometimes zero-padded or four digits), else
 * empty, a sign, a space, or any byte. */
static size_t ipv4_part(uint32_t *state, char *out)
{
    switch (below(state, 24u)) {
    case 0u:
        return 0u;
    case 1u:
        return (size_t)snprintf(out, PART, "0%u", below(state, 300u));
    case 2u:
        return (size_t)snprintf(out, PART, "%u", 1000u + below(state, 9000u));
    case 3u:
        out[0] = "+- x"[below(state, 4u)];
        return 1u + (size_t)snprintf(out + 1, PART - 1u, "%u", below(state, 256u));
    case 4u:
        out[0] = (char)below(state, 256u);
        return 1u;
    default:
        return (size_t)snprintf(out, PART, "%u", below(state, 300u));
    }
}

/* Two to six parts, usually joined by '.', with occasional trailing garbage; an eighth
 * of the cases are bytes from the full 0..255 range instead. */
static void ipv4_text(uint32_t *state, char *text)
{
    size_t used = 0u;
    if (below(state, 8u) == 0u) {
        size_t length = below(state, 20u);
        for (; used < length; ++used)
            text[used] = (char)below(state, 256u);
        text[used] = '\0';
        return;
    }
    unsigned parts = below(state, 5u) ? 4u : 2u + below(state, 5u);
    for (unsigned p = 0u; p < parts; ++p) {
        if (p)
            text[used++] = below(state, 25u) ? '.' : (char)below(state, 256u);
        used += ipv4_part(state, text + used);
    }
    if (below(state, 15u) == 0u)
        text[used++] = (char)below(state, 256u);
    text[used] = '\0';
}

static unsigned fuzz_ipv4(void)
{
    unsigned accepted = 0u;
    for (uint32_t seed = 0u; seed < SEEDS; ++seed) {
        uint32_t state = seed_state(seed);
        char text[TEXT];
        uint8_t got[4] = {7u, 7u, 7u, 7u}, want[4] = {0u, 0u, 0u, 0u};
        ipv4_text(&state, text);
        bool expected = reference_ipv4(text, want);
        bool ok = jelli_agent_ipv4_parse(text, got);
        CHECK_CASE(ok == expected, seed, text);
        if (ok)
            CHECK_CASE(memcmp(got, want, sizeof(got)) == 0, seed, text);
        else
            CHECK_CASE(got[0] == 7u && got[1] == 7u && got[2] == 7u && got[3] == 7u, seed, text);
        accepted += ok;
    }
    return accepted;
}

/* Metamorphic: any address printed canonically parses back to itself; a non-digit
 * appended or prepended, or a zero inserted before a nonzero part, is refused. */
static void ipv4_round_trips(void)
{
    for (uint32_t seed = 0u; seed < SEEDS / 4u; ++seed) {
        uint32_t state = seed_state(seed);
        uint8_t address[4], parsed[4];
        char text[TEXT], changed[TEXT];
        for (unsigned i = 0u; i < 4u; ++i)
            address[i] = (uint8_t)below(&state, 256u);
        (void)snprintf(text, sizeof(text), "%u.%u.%u.%u", address[0], address[1], address[2],
                       address[3]);
        CHECK_CASE(jelli_agent_ipv4_parse(text, parsed), seed, text);
        CHECK_CASE(memcmp(parsed, address, sizeof(parsed)) == 0, seed, text);
        char extra = (char)(1u + below(&state, 255u));
        if (extra >= '0' && extra <= '9')
            extra = '/';
        (void)snprintf(changed, sizeof(changed), "%s%c", text, extra);
        CHECK_CASE(!jelli_agent_ipv4_parse(changed, parsed), seed, changed);
        (void)snprintf(changed, sizeof(changed), "%c%s", extra, text);
        CHECK_CASE(!jelli_agent_ipv4_parse(changed, parsed), seed, changed);
        unsigned part = below(&state, 4u);
        if (address[part]) {
            const char *at = text;
            for (unsigned dots = 0u; dots < part; ++at)
                dots += *at == '.';
            (void)snprintf(changed, sizeof(changed), "%.*s0%s", (int)(at - text), text, at);
            CHECK_CASE(!jelli_agent_ipv4_parse(changed, parsed), seed, changed);
        }
    }
}

/* Reads the header the way spike_server.py does with struct "<2sBBII". */
static uint32_t read_u32_le(const uint8_t *in)
{
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

static void header_round_trips(void)
{
    for (uint32_t seed = 0u; seed < SEEDS; ++seed) {
        uint32_t state = seed_state(seed);
        uint8_t buffer[JELLI_AGENT_FRAME_HEADER + GUARD];
        uint8_t channels = (uint8_t)below(&state, 256u);
        uint32_t sequence = next_random(&state), capture_ms = next_random(&state);
        memset(buffer, SENTINEL, sizeof(buffer));
        jelli_agent_frame_header(buffer, channels, sequence, capture_ms);
        CHECK_CASE(buffer[0] == 'J' && buffer[1] == 'A' && buffer[2] == 1u, seed, "");
        CHECK_CASE(buffer[3] == channels, seed, "");
        CHECK_CASE(read_u32_le(buffer + 4) == sequence, seed, "");
        CHECK_CASE(read_u32_le(buffer + 8) == capture_ms, seed, "");
        for (size_t i = JELLI_AGENT_FRAME_HEADER; i < sizeof(buffer); ++i)
            CHECK_CASE(buffer[i] == SENTINEL, seed, "");
    }
}

int main(void)
{
    unsigned hex = fuzz_hex(), ipv4 = fuzz_ipv4();
    /* The generators must exercise both outcomes, or the comparison proves little. */
    if (hex < SEEDS / 10u || hex > SEEDS - SEEDS / 10u || ipv4 < SEEDS / 10u ||
        ipv4 > SEEDS - SEEDS / 10u) {
        fprintf(stderr, "Unbalanced generators: hex %u, ipv4 %u accepted of %u\n", hex, ipv4,
                (unsigned)SEEDS);
        return 1;
    }
    hex_round_trips();
    ipv4_round_trips();
    header_round_trips();
    printf("Agent argument fuzz passed: hex %u/%u, ipv4 %u/%u accepted\n", hex, (unsigned)SEEDS,
           ipv4, (unsigned)SEEDS);
    return 0;
}

/* Property and metamorphic tests for jelli_agent_host_valid against a strict model of
 * lwIP's ip4addr_aton, which the board's getaddrinfo tries before DNS: every name the
 * board accepts must be refused by lwIP as a number, and every accepted string must be
 * either a canonical dotted quad or such a name. Each case uses a fixed seed. */
#include "jelli/agent_args.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_CASE(expr, seed, text)                                                               \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed %u, input \"%s\")\n", __FILE__, __LINE__, #expr,      \
                    (unsigned)(seed), (text));                                                     \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SEEDS = 40000u, TEXT = 96u, LABELS = 5u };

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

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_xdigit(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static bool is_space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* One number as lwIP reads it: 0x hex, 0 octal, else decimal, wrapping in 32 bits. */
static const char *lwip_number(const char *cp, uint32_t *value)
{
    uint32_t base = 10u, val = 0u;
    if (*cp == '0') {
        ++cp;
        base = (*cp == 'x' || *cp == 'X') ? 16u : 8u;
        cp += base == 16u;
    }
    for (;; ++cp) {
        if (is_digit(*cp) && !(base == 8u && *cp >= '8'))
            val = val * base + (uint32_t)(*cp - '0');
        else if (base == 16u && is_xdigit(*cp))
            val = (val << 4) | (uint32_t)(*cp + 10 - (*cp >= 'a' ? 'a' : 'A'));
        else
            break;
    }
    *value = val;
    return cp;
}

/* lwIP 2.x ip4addr_aton, step for step: one to four parts, the last filling the rest,
 * trailing whitespace allowed. Writes the address in network order. */
static bool lwip_aton(const char *cp, uint8_t address[4])
{
    uint32_t parts[4], val = 0u;
    unsigned count = 0u;
    for (;;) {
        if (!is_digit(*cp))
            return false;
        cp = lwip_number(cp, &val);
        if (*cp != '.')
            break;
        if (count >= 3u)
            return false;
        parts[count++] = val;
        ++cp;
    }
    if (*cp != '\0' && !is_space(*cp))
        return false;
    static const uint32_t last_max[4] = {0xffffffffu, 0xffffffu, 0xffffu, 0xffu};
    if (val > last_max[count])
        return false;
    for (unsigned i = 0u; i < count; ++i) {
        if (parts[i] > 0xffu)
            return false;
        val |= parts[i] << (24u - 8u * i);
    }
    for (unsigned i = 0u; i < 4u; ++i)
        address[i] = (uint8_t)(val >> (24u - 8u * i));
    return true;
}

/* A canonical dotted quad: lwIP reads it, and it prints back exactly as written. */
static bool canonical_quad(const char *text, uint8_t address[4])
{
    char printed[TEXT];
    if (!lwip_aton(text, address))
        return false;
    (void)snprintf(printed, sizeof(printed), "%u.%u.%u.%u", address[0], address[1], address[2],
                   address[3]);
    return strcmp(printed, text) == 0;
}

/* RFC 1123 labels split by hand, the last starting with a letter, at most 63 bytes. */
static bool reference_name(const char *text)
{
    size_t length = strlen(text);
    if (!length || length > 63u)
        return false;
    for (const char *label = text;;) {
        const char *dot = strchr(label, '.');
        size_t size = dot ? (size_t)(dot - label) : strlen(label);
        if (!size || label[0] == '-' || label[size - 1u] == '-')
            return false;
        for (size_t i = 0u; i < size; ++i)
            if (!is_alpha(label[i]) && !is_digit(label[i]) && label[i] != '-')
                return false;
        if (!dot)
            return is_alpha(label[0]);
        label = dot + 1;
    }
}

/* One label: numbers lwIP reads (decimal, octal, hex, large), words, dashes, or junk. */
static size_t label_text(uint32_t *state, char *out, size_t room)
{
    static const char alnum[] = "abcxyzABCXYZ0123456789-";
    switch (below(state, 10u)) {
    case 0u:
        return (size_t)snprintf(out, room, "0%o", below(state, 600u));
    case 1u:
        return (size_t)snprintf(out, room, "0x%x", below(state, 0x200u) << (8u * below(state, 4u)));
    case 2u:
        return (size_t)snprintf(out, room, "%u", next_random(state));
    case 3u:
    case 4u:
        return (size_t)snprintf(out, room, "%u", below(state, 300u));
    case 5u:
        out[0] = (char)(1u + below(state, 255u));
        return 1u;
    default: {
        size_t size = 1u + below(state, 8u);
        for (size_t i = 0u; i < size; ++i)
            out[i] = alnum[below(state, sizeof(alnum) - 1u)];
        return size;
    }
    }
}

/* One to five labels; a sixth of the cases are four mostly small decimals, near a quad. */
static void host_text(uint32_t *state, char *text)
{
    size_t used = 0u;
    bool quadish = below(state, 6u) == 0u;
    unsigned labels = quadish ? 4u : 1u + below(state, LABELS);
    for (unsigned l = 0u; l < labels; ++l) {
        if (l)
            text[used++] = below(state, 30u) ? '.' : "-. _"[below(state, 4u)];
        if (quadish && below(state, 8u))
            used += (size_t)snprintf(text + used, 16u, "%u", below(state, 260u));
        else if (below(state, 30u))
            used += label_text(state, text + used, 16u);
    }
    if (below(state, 20u) == 0u)
        text[used++] = " \t.\n"[below(state, 4u)];
    text[used] = '\0';
}

struct tally {
    unsigned names, quads, numbers_refused;
};

/* The three properties for one string; counts which kind it was. */
static void check_host(const char *text, uint32_t seed, struct tally *tally)
{
    uint8_t lwip[4], ours[4], quad[4];
    bool accepted = jelli_agent_host_valid(text);
    bool canonical = canonical_quad(text, quad);
    bool name = reference_name(text);
    bool number = lwip_aton(text, lwip);
    /* Accepted exactly when a canonical quad or a sound name. */
    CHECK_CASE(accepted == (canonical || name), seed, text);
    /* No name reads as a number to lwIP, so no name dials an address by surprise. */
    CHECK_CASE(!name || !number, seed, text);
    /* A quad means the same address to both parsers. */
    if (canonical)
        CHECK_CASE(jelli_agent_ipv4_parse(text, ours) && memcmp(ours, quad, 4u) == 0, seed, text);
    tally->names += accepted && !canonical;
    tally->quads += canonical;
    tally->numbers_refused += number && !accepted;
}

/* Metamorphic, for an accepted string: case does not matter; another label starting with
 * a letter keeps it accepted; a last label starting with a digit refuses a name. */
static void check_relations(const char *text, uint32_t seed)
{
    char changed[TEXT];
    size_t length = strlen(text);
    if (!jelli_agent_host_valid(text) || length > 57u)
        return;
    for (size_t i = 0u; i <= length; ++i) {
        char c = text[i];
        changed[i] = (char)(is_alpha(c) ? c ^ 0x20 : c);
    }
    CHECK_CASE(jelli_agent_host_valid(changed), seed, changed);
    (void)snprintf(changed, sizeof(changed), "%s.local", text);
    CHECK_CASE(jelli_agent_host_valid(changed), seed, changed);
    (void)snprintf(changed, sizeof(changed), "%s.1", text);
    bool quad = jelli_agent_ipv4_parse(changed, (uint8_t[4]){0});
    CHECK_CASE(quad || !jelli_agent_host_valid(changed), seed, changed);
    (void)snprintf(changed, sizeof(changed), "%s-", text);
    CHECK_CASE(!jelli_agent_host_valid(changed), seed, changed);
    /* A name padded with a leading label to exactly 63 bytes passes; to 64 it does not. */
    static const char pad[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    if (quad || length > 61u || jelli_agent_ipv4_parse(text, (uint8_t[4]){0}))
        return;
    (void)snprintf(changed, sizeof(changed), "%.*s.%s", (int)(62u - length), pad, text);
    CHECK_CASE(strlen(changed) == 63u && jelli_agent_host_valid(changed), seed, changed);
    (void)snprintf(changed, sizeof(changed), "%.*s.%s", (int)(63u - length), pad, text);
    CHECK_CASE(!jelli_agent_host_valid(changed), seed, changed);
}

/* Inputs lwIP reads as addresses in ways a person would not, pinned; each must be refused
 * by the board, and the model must agree with lwIP's documented readings. */
static void pinned(void)
{
    static const struct {
        const char *text;
        uint8_t lwip[4];
    } numbers[] = {
        {"0x7f000001", {127u, 0u, 0u, 1u}},
        {"0X7F.1", {127u, 0u, 0u, 1u}},
        {"017700000001", {127u, 0u, 0u, 1u}},
        {"127.1", {127u, 0u, 0u, 1u}},
        {"010.0.0.1", {8u, 0u, 0u, 1u}},
        {"1.2.3.4 ", {1u, 2u, 3u, 4u}},
        {"1.2.3.4\tx", {1u, 2u, 3u, 4u}},
        {"4294967296", {0u, 0u, 0u, 0u}},
        {"0x", {0u, 0u, 0u, 0u}},
        {"0.0x.0", {0u, 0u, 0u, 0u}},
    };
    static const char *const names[] = {"a.0x1", "0x7f000001.local", "1e1", "studio.9a"};
    uint8_t address[4];
    struct tally tally = {0u, 0u, 0u};
    for (size_t i = 0u; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        CHECK_CASE(lwip_aton(numbers[i].text, address), i, numbers[i].text);
        CHECK_CASE(memcmp(address, numbers[i].lwip, 4u) == 0, i, numbers[i].text);
        CHECK_CASE(!jelli_agent_host_valid(numbers[i].text), i, numbers[i].text);
        check_host(numbers[i].text, (uint32_t)i, &tally);
    }
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i)
        check_host(names[i], (uint32_t)i, &tally);
    CHECK_CASE(tally.numbers_refused == sizeof(numbers) / sizeof(numbers[0]), 0u, "pinned");
    CHECK_CASE(jelli_agent_host_valid("0x7f000001.local") &&
                   !lwip_aton("0x7f000001.local", address),
               0u, "0x7f000001.local");
}

int main(void)
{
    struct tally tally = {0u, 0u, 0u};
    pinned();
    for (uint32_t seed = 0u; seed < SEEDS; ++seed) {
        uint32_t state = seed * 2654435761u + 0x9e3779b9u;
        char text[TEXT];
        host_text(&state, text);
        check_host(text, seed, &tally);
        check_relations(text, seed);
    }
    /* Each kind must be common, or the comparison proves little. */
    if (tally.names < SEEDS / 20u || tally.quads < SEEDS / 200u ||
        tally.numbers_refused < SEEDS / 50u) {
        fprintf(stderr, "Unbalanced generator: %u names, %u quads, %u numbers refused of %u\n",
                tally.names, tally.quads, tally.numbers_refused, (unsigned)SEEDS);
        return 1;
    }
    printf("Agent host fuzz passed: %u names, %u quads accepted; %u lwIP numbers refused of %u\n",
           tally.names, tally.quads, tally.numbers_refused, (unsigned)SEEDS);
    return 0;
}

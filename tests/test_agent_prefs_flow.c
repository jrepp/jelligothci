#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Settings' flow, white-box (ADR-016): a scan lists each name and lock once at its
 * strongest, the strongest sixteen, as an oracle computes it; opening, joining, and ticking at
 * their edges. Seeded; failures name the seed. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: seed %u step %u: %s\n", __FILE__, __LINE__, seed, step,        \
                    #expr);                                                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned seed, step;
static uint32_t state;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/* One listing: a name and its lock, so an open network cannot take a locked one's row. */
static bool same_entry(const JelliPrefsNetwork *a, const JelliPrefsNetwork *b)
{
    return a->ssid_size == b->ssid_size && a->security == b->security &&
           !memcmp(a->ssid, b->ssid, a->ssid_size);
}

/* Names of one or two letters from five, so they repeat; bytes past a name are noise. */
static size_t random_found(JelliPrefsNetwork *found, size_t capacity)
{
    size_t count = next() % (capacity + 1u);
    for (size_t i = 0u; i < count; ++i) {
        uint8_t size = (uint8_t)(1u + next() % 2u);
        if (next() % 10u == 0u)
            size = (next() & 1u) ? 0u : JELLI_PREFS_SSID_MAX + 1u; /* Never listed. */
        found[i] = (JelliPrefsNetwork){{0}, size, (int8_t)next(), (uint8_t)(next() % 3u)};
        for (unsigned b = 0u; b < JELLI_PREFS_SSID_MAX; ++b)
            found[i].ssid[b] = (uint8_t)(b < 2u ? 'a' + next() % 5u : next());
    }
    return count;
}

/* The oracle: each listable name and lock's strongest signal, strongest first. */
static size_t strongest(const JelliPrefsNetwork *found, size_t count, int8_t *best,
                        JelliPrefsNetwork *names)
{
    size_t distinct = 0u;
    for (size_t i = 0u; i < count; ++i) {
        if (!found[i].ssid_size || found[i].ssid_size > JELLI_PREFS_SSID_MAX)
            continue;
        size_t at = 0u;
        while (at < distinct && !same_entry(&names[at], &found[i]))
            ++at;
        if (at == distinct) {
            names[distinct] = found[i];
            best[distinct++] = found[i].rssi;
        } else if (found[i].rssi > best[at]) {
            best[at] = found[i].rssi;
        }
    }
    return distinct;
}

/* Where the oracle has this name; distinct when it has not. */
static size_t oracle_at(const JelliPrefsNetwork *names, size_t distinct,
                        const JelliPrefsNetwork *network)
{
    size_t at = 0u;
    while (at < distinct && !same_entry(&names[at], network))
        ++at;
    return at;
}

static void test_scan_keeps_each_name_at_its_strongest(void)
{
    for (seed = 0u; seed < 3000u; ++seed) {
        JelliPrefsNetwork found[60], names[60];
        int8_t best[60];
        JelliPrefs prefs;
        state = 0x6a09e667u ^ (seed * 2654435761u);
        size_t count = random_found(found, 60u), distinct = strongest(found, count, best, names);
        jelli_prefs_init(&prefs);
        jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
        (void)jelli_prefs_tap(&prefs, 233, 188, 0u, NULL);
        jelli_prefs_scanned(&prefs, found, count, true, 0u);
        size_t want = distinct < JELLI_PREFS_MAX_NETWORKS ? distinct : JELLI_PREFS_MAX_NETWORKS;
        CHECK(prefs.count == want && prefs.top == 0u && !prefs.scan_failed);
        for (size_t i = 0u; i < distinct; ++i) /* Sort the oracle's signals, strongest first. */
            for (size_t j = i + 1u; j < distinct; ++j)
                if (best[j] > best[i]) {
                    int8_t swap = best[i];
                    best[i] = best[j];
                    best[j] = swap;
                    JelliPrefsNetwork name = names[i];
                    names[i] = names[j];
                    names[j] = name;
                }
        for (step = 0u; step < prefs.count; ++step) {
            const JelliPrefsNetwork *listed = &prefs.networks[step];
            CHECK(listed->rssi == best[step]);              /* The strongest sixteen signals... */
            size_t at = oracle_at(names, distinct, listed); /* ...each its own name's. */
            CHECK(at < distinct && listed->rssi == best[at]);
            for (unsigned j = 0u; j < step; ++j)
                CHECK(!same_entry(&prefs.networks[j], listed));
        }
        step = 0u;
    }
}

/* Opening takes a name of 1..32 bytes, online only with one, and starts afresh. */
static void test_opening_at_its_edges(void)
{
    uint8_t name[40];
    JelliPrefs prefs;
    memset(name, 'n', sizeof(name));
    seed = step = 0u;
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, name, JELLI_PREFS_SSID_MAX, true, 77u);
    CHECK(prefs.current_size == JELLI_PREFS_SSID_MAX && prefs.connected && prefs.since_ms == 77u);
    jelli_prefs_open(&prefs, name, JELLI_PREFS_SSID_MAX + 1u, true, 0u);
    CHECK(prefs.current_size == 0u && !prefs.connected && prefs.current[0] == 0u);
    jelli_prefs_open(&prefs, NULL, 5u, true, 0u);
    CHECK(prefs.current_size == 0u && !prefs.connected);
    jelli_prefs_open(&prefs, name, 0u, true, 0u);
    CHECK(prefs.current_size == 0u && !prefs.connected && prefs.screen == JELLI_PREFS_MENU);
    (void)jelli_prefs_tap(&prefs, 233, 188, 0u, NULL);
    JelliPrefsNetwork home = {{'H'}, 1u, -40, JELLI_PREFS_PASSWORD_NETWORK};
    jelli_prefs_scanned(&prefs, &home, 1u, true, 0u);
    (void)jelli_prefs_tap(&prefs, PREFS_LIST_X + 5, PREFS_LIST_Y + 5, 0u, NULL);
    memcpy(prefs.password, "typed so far", 12u);
    prefs.password_size = 12u;
    prefs.reveal = true;
    prefs.page = PREFS_SYMBOLS;
    jelli_prefs_open(&prefs, name, 3u, false, 9u); /* Reopened mid-typing: all forgotten. */
    CHECK(prefs.screen == JELLI_PREFS_MENU && prefs.count == 0u && prefs.password_size == 0u);
    CHECK(!prefs.reveal && prefs.password[0] == '\0' && prefs.current_size == 3u);
    CHECK(prefs.dirty && prefs.frame == 0u && prefs.drag == 0);
    jelli_prefs_open(NULL, name, 3u, true, 0u);
    jelli_prefs_close(NULL);
    jelli_prefs_init(NULL);
    jelli_prefs_drag(NULL, 5);
    jelli_prefs_scanned(NULL, &home, 1u, true, 0u);
    jelli_prefs_joined(NULL, JELLI_PREFS_JOINED, 0u);
    CHECK(!jelli_prefs_tick(NULL, 0u));
    CHECK(jelli_prefs_tap(NULL, 0, 0, 0u, NULL) == JELLI_PREFS_DO_NOTHING);
}

/* An open network chosen from a list of n, joining, then told how it went. */
static void joining(JelliPrefs *prefs, uint64_t now)
{
    JelliPrefsNetwork found[2] = {{{'A', 'B'}, 2u, -40, 0u}, {{'C'}, 1u, -60, 0u}};
    JelliPrefsJoin join;
    jelli_prefs_init(prefs);
    jelli_prefs_open(prefs, (const uint8_t *)"Old", 3u, true, now);
    (void)jelli_prefs_tap(prefs, 233, 188, now, NULL);
    jelli_prefs_scanned(prefs, found, 2u, true, now);
    CHECK(jelli_prefs_tap(prefs, PREFS_LIST_X + 5, PREFS_LIST_Y + PREFS_ROW_PITCH + 5, now,
                          &join) == JELLI_PREFS_DO_JOIN);
    CHECK(prefs->screen == JELLI_PREFS_JOINING && prefs->chosen == 1u && join.ssid[0] == 'C');
}

/* Only known outcomes count; only a join changes the network shown; only a success
 * moves on by itself, after exactly JELLI_PREFS_RESULT_MS; only waiting animates. */
static void test_outcomes_and_ticks_at_their_edges(void)
{
    JelliPrefs prefs;
    seed = step = 0u;
    joining(&prefs, 1000u);
    /* Out of range on purpose: the host's outcome is bounded before it is shown. */
    /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
    jelli_prefs_joined(&prefs, (JelliPrefsOutcome)5, 1000u);
    /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
    jelli_prefs_joined(&prefs, (JelliPrefsOutcome)99, 1000u);
    CHECK(prefs.screen == JELLI_PREFS_JOINING);
    prefs.dirty = false;
    for (unsigned frame = 1u; frame <= 8u; ++frame) { /* Dots: 1, 2, 3, none, 1, ... */
        CHECK(jelli_prefs_tick(&prefs, 1000u + frame * JELLI_PREFS_ANIMATE_MS));
        CHECK(prefs.frame == frame % 4u);
        prefs.dirty = false;
    }
    CHECK(!jelli_prefs_tick(&prefs, 1000u + 8u * JELLI_PREFS_ANIMATE_MS + 1u));
    CHECK(!jelli_prefs_tick(&prefs, UINT64_MAX) || prefs.frame == (UINT64_MAX - 1000u) / 400u % 4u);
    jelli_prefs_joined(&prefs, JELLI_PREFS_FAILED, 5000u);
    CHECK(prefs.current_size == 3u && !memcmp(prefs.current, "Old", 3u) && prefs.connected);
    prefs.dirty = false;
    CHECK(!jelli_prefs_tick(&prefs, 5000u + 1000000000u) && prefs.screen == JELLI_PREFS_RESULT);
    CHECK(!jelli_prefs_tick(&prefs, 0u)); /* A clock running backwards. */
    joining(&prefs, 1000u);
    jelli_prefs_joined(&prefs, JELLI_PREFS_JOINED, 7000u);
    CHECK(prefs.current_size == 1u && prefs.current[0] == 'C' && prefs.current[1] == 0u);
    CHECK(prefs.connected && prefs.outcome == JELLI_PREFS_JOINED);
    jelli_prefs_joined(&prefs, JELLI_PREFS_FAILED, 7000u); /* Too late: not joining now. */
    CHECK(prefs.outcome == JELLI_PREFS_JOINED);
    prefs.dirty = false;
    CHECK(!jelli_prefs_tick(&prefs, 6000u) && prefs.screen == JELLI_PREFS_RESULT);
    CHECK(!jelli_prefs_tick(&prefs, 7000u + JELLI_PREFS_RESULT_MS - 1u));
    CHECK(jelli_prefs_tick(&prefs, 7000u + JELLI_PREFS_RESULT_MS));
    CHECK(prefs.screen == JELLI_PREFS_MENU && prefs.since_ms == 7000u + JELLI_PREFS_RESULT_MS);
    prefs.dirty = false;
    CHECK(!jelli_prefs_tick(&prefs, UINT64_MAX)); /* Settings never animates. */
    joining(&prefs, 0u);
    jelli_prefs_joined(&prefs, JELLI_PREFS_JOINED, 0u);
    (void)jelli_prefs_tap(&prefs, -5, -5, 10u, NULL); /* Any tap dismisses a success. */
    CHECK(prefs.screen == JELLI_PREFS_MENU);
}

int main(void)
{
    test_scan_keeps_each_name_at_its_strongest();
    test_opening_at_its_edges();
    test_outcomes_and_ticks_at_their_edges();
    printf("Settings flow properties passed\n");
    return 0;
}

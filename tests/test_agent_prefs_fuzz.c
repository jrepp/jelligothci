#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Property: any mix of taps, drags, scan and join results, ticks, and closes keeps
 * Settings consistent, and a password is never held outside the password screen. */

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

static int coordinate(void)
{
    switch (next() % 8u) {
    case 0:
        return (int)(next() % 4096u) - 2048; /* Anywhere, even off the screen. */
    case 1:
        return (next() & 1u) ? -2147483647 - 1 : 2147483647;
    default:
        return (int)(next() % 466u);
    }
}

static void random_scan(JelliPrefs *prefs, uint64_t now)
{
    JelliPrefsNetwork found[40];
    size_t count = next() % 41u;
    for (size_t i = 0u; i < count; ++i) {
        found[i] = (JelliPrefsNetwork){
            {0}, (uint8_t)(next() % 40u), (int8_t)next(), (uint8_t)(next() % 5u)};
        for (unsigned b = 0u; b < JELLI_PREFS_SSID_MAX; ++b)
            found[i].ssid[b] = (uint8_t)((next() % 4u) ? 'a' + next() % 3u : next());
    }
    jelli_prefs_scanned(prefs, found, count, next() % 5u != 0u, now);
}

static bool printable(const JelliPrefs *prefs)
{
    for (unsigned i = 0u; i < prefs->password_size; ++i)
        if (prefs->password[i] < ' ' || prefs->password[i] > '~')
            return false;
    for (size_t i = prefs->password_size; i < sizeof(prefs->password); ++i)
        if (prefs->password[i])
            return false;
    return true;
}

static void invariants(const JelliPrefs *prefs, const JelliPrefsJoin *join, JelliPrefsAction action)
{
    CHECK(prefs->screen <= JELLI_PREFS_RESULT);
    CHECK(prefs->password_size <= JELLI_PREFS_PASSWORD_MAX && printable(prefs));
    CHECK(prefs->screen == JELLI_PREFS_PASSWORD || prefs->password_size == 0u);
    CHECK(prefs->count <= JELLI_PREFS_MAX_NETWORKS);
    CHECK(prefs->top == 0u || prefs->top + PREFS_LIST_ROWS <= prefs->count);
    CHECK(prefs->page < PREFS_PAGE_COUNT);
    for (unsigned i = 0u; i < prefs->count; ++i) {
        CHECK(prefs->networks[i].ssid_size >= 1u && prefs->networks[i].ssid_size <= 32u);
        CHECK(i == 0u || prefs->networks[i - 1u].rssi >= prefs->networks[i].rssi);
    }
    if (prefs->screen == JELLI_PREFS_PASSWORD || prefs->screen == JELLI_PREFS_JOINING)
        CHECK(prefs->chosen < prefs->count);
    if (action == JELLI_PREFS_DO_JOIN) {
        CHECK(prefs->screen == JELLI_PREFS_JOINING);
        CHECK(join->ssid_size >= 1u && join->ssid_size <= JELLI_PREFS_SSID_MAX);
        CHECK(join->password_size == 0u || join->password_size >= JELLI_PREFS_PASSWORD_MIN);
        CHECK(join->password[join->password_size] == '\0');
    }
    if (action == JELLI_PREFS_DO_CLOSE)
        CHECK(prefs->screen == JELLI_PREFS_CLOSED);
}

static void run(unsigned which)
{
    JelliPrefs prefs;
    JelliPrefsJoin join;
    uint8_t current[40]; /* What the host says it uses; any size, even too long. */
    uint64_t now = 0u;
    seed = which;
    memset(current, 'h', sizeof(current));
    state = 0x9e3779b9u ^ (which * 2654435761u) ^ 1u;
    jelli_prefs_init(&prefs);
    for (step = 0u; step < 4000u; ++step) {
        now += next() % 700u;
        JelliPrefsAction action = JELLI_PREFS_DO_NOTHING;
        switch (next() % 12u) {
        case 0:
            jelli_prefs_open(&prefs, current, next() % 40u, next() & 1u, now);
            break;
        case 1:
            jelli_prefs_close(&prefs);
            break;
        case 2:
            random_scan(&prefs, now);
            break;
        case 3:
            jelli_prefs_joined(&prefs, (JelliPrefsOutcome)(next() % 7u), now);
            break;
        case 4:
            jelli_prefs_drag(&prefs, coordinate());
            break;
        case 5:
            (void)jelli_prefs_tick(&prefs, now);
            break;
        default:
            action = jelli_prefs_tap(&prefs, coordinate(), coordinate(), now, &join);
            break;
        }
        invariants(&prefs, &join, action);
    }
}

int main(void)
{
    for (unsigned which = 0u; which < 300u; ++which)
        run(which);
    printf("Settings fuzz checks passed (300 seeds of 4000 steps)\n");
    return 0;
}

#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include "jelli/pet_screen.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Typing a password against a model (ADR-016), under seeded runs of taps aimed at each
 * screen, the side button, scan and join results, ticks, drags, and the screen going
 * dark after two idle minutes, which closes Settings as the agent's firmware does. The
 * password is only ever on the password screen and is exactly what was typed there; a
 * join hands over exactly that (nothing for an open network) with the rest zero; and
 * every way out wipes it: Cancel, Join, reopening, closing, Done, and the dark. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: seed %u step %u: %s\n", __FILE__, __LINE__, seed, step,        \
                    #expr);                                                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { IDLE_MS = 120000, SEEDS = 200, STEPS = 3000 };

typedef struct {
    char password[JELLI_PREFS_PASSWORD_MAX + 1]; /* Zero past size, like the real one. */
    size_t size;
} Typed;

static unsigned seed, step;
static unsigned joins_with_password, darkened_mid_typing, cancels, reopened_mid_typing;
static uint32_t state;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static bool zero(const void *memory, size_t from, size_t to)
{
    for (size_t i = from; i < to; ++i)
        if (((const uint8_t *)memory)[i])
            return false;
    return true;
}

static bool wiped(const JelliPrefs *prefs)
{
    return prefs->password_size == 0u && !prefs->reveal &&
           zero(prefs->password, 0u, sizeof(prefs->password));
}

/* The model: a key typed; true when it asks to join with enough characters. */
static bool model_key(Typed *typed, const PrefsKey *key)
{
    switch (key->kind) {
    case PREFS_KEY_CHAR:
    case PREFS_KEY_SPACE:
        if (typed->size < JELLI_PREFS_PASSWORD_MAX)
            typed->password[typed->size++] = key->ch;
        return false;
    case PREFS_KEY_DELETE:
        if (typed->size)
            typed->password[--typed->size] = '\0';
        return false;
    case PREFS_KEY_JOIN:
        return typed->size >= JELLI_PREFS_PASSWORD_MIN;
    case PREFS_KEY_SHIFT: /* Page keys change what is shown, not what is typed. */
    case PREFS_KEY_MORE:
    case PREFS_KEY_DIGITS:
    case PREFS_KEY_LETTERS:
        return false;
    }
    return false;
}

/* A request names the network and carries exactly the typed password, zero after both. */
static void check_request(const JelliPrefsJoin *join, const JelliPrefsNetwork *network,
                          const Typed *typed)
{
    size_t password = network->security == JELLI_PREFS_PASSWORD_NETWORK ? typed->size : 0u;
    CHECK(join->ssid_size == network->ssid_size);
    CHECK(!memcmp(join->ssid, network->ssid, network->ssid_size));
    CHECK(zero(join->ssid, network->ssid_size, sizeof(join->ssid)));
    CHECK(join->password_size == password && !memcmp(join->password, typed->password, password));
    CHECK(zero(join->password, password, sizeof(join->password)));
}

static void forget(Typed *typed) { memset(typed, 0, sizeof(*typed)); }

/* Somewhere worth tapping on the screen shown: keys, rows, and buttons, or anywhere. */
static void aim(const JelliPrefs *prefs, int *x, int *y)
{
    static const PrefsPart parts[] = {PREFS_MENU_WIFI,   PREFS_LIST_SCAN,    PREFS_LIST_BACK,
                                      PREFS_PASS_SHOW,   PREFS_PASS_CANCEL,  PREFS_RESULT_RETRY,
                                      PREFS_RESULT_BACK, PREFS_RESULT_ALONE, PREFS_MENU_DONE};
    PrefsKey keys[PREFS_MAX_KEYS];
    PrefsBox box = prefs_row_box(next() % PREFS_LIST_ROWS);
    unsigned roll = next() % 20u;
    size_t count = prefs_keys(prefs->page, keys, PREFS_MAX_KEYS);
    if (roll < 12u && prefs->screen == JELLI_PREFS_PASSWORD)
        box = keys[next() % count].box;
    else if (roll < 14u && prefs->screen == JELLI_PREFS_PASSWORD) /* Join: the last key. */
        box = keys[count - 1u].box;
    else if (roll < 12u && prefs->screen == JELLI_PREFS_LIST)
        box = prefs_row_box(next() % PREFS_LIST_ROWS);
    else if (roll < 18u)
        box = prefs_box(parts[next() % (sizeof(parts) / sizeof(parts[0]) - (roll & 1u))]);
    else if (roll == 18u)
        box = (PrefsBox){0, 0, 466, 466};
    *x = box.x + (int)(next() % (unsigned)box.width);
    *y = box.y + (int)(next() % (unsigned)box.height);
}

/* A tap on the password screen does what the model says, and nothing else. */
static void tap_password(JelliPrefs *prefs, Typed *typed, int x, int y, uint64_t now)
{
    PrefsKey key;
    bool cancel = prefs_inside(prefs_box(PREFS_PASS_CANCEL), x, y);
    bool show = !cancel && prefs_inside(prefs_box(PREFS_PASS_SHOW), x, y);
    bool on_key = !cancel && !show && prefs_key_at(prefs->page, x, y, &key);
    bool reveal = prefs->reveal, joins = on_key && model_key(typed, &key);
    JelliPrefsNetwork network = prefs->networks[prefs->chosen];
    JelliPrefsJoin join;
    memset(&join, 0xa5, sizeof(join));
    JelliPrefsAction action = jelli_prefs_tap(prefs, x, y, now, &join);
    CHECK(action == (joins ? JELLI_PREFS_DO_JOIN : JELLI_PREFS_DO_NOTHING));
    if (joins) {
        check_request(&join, &network, typed);
        joins_with_password += network.security == JELLI_PREFS_PASSWORD_NETWORK;
    } else {
        for (size_t i = 0u; i < sizeof(join); ++i) /* Left alone unless joining. */
            CHECK(((const uint8_t *)&join)[i] == 0xa5u);
    }
    CHECK(prefs->screen == (cancel  ? JELLI_PREFS_LIST
                            : joins ? JELLI_PREFS_JOINING
                                    : JELLI_PREFS_PASSWORD));
    CHECK(prefs->reveal == (show ? !reveal : prefs->screen == JELLI_PREFS_PASSWORD && reveal));
    cancels += cancel && typed->size;
    if (prefs->screen != JELLI_PREFS_PASSWORD)
        forget(typed);
}

static void tap_elsewhere(JelliPrefs *prefs, int x, int y, uint64_t now)
{
    static const Typed none = {{0}, 0u};
    JelliPrefsJoin join;
    memset(&join, 0xa5, sizeof(join));
    JelliPrefsAction action = jelli_prefs_tap(prefs, x, y, now, &join);
    if (action == JELLI_PREFS_DO_JOIN) /* An open network, from the list or Try again. */
        check_request(&join, &prefs->networks[prefs->chosen], &none);
    CHECK(action != JELLI_PREFS_DO_JOIN ||
          prefs->networks[prefs->chosen].security == JELLI_PREFS_OPEN_NETWORK);
}

static void scan(JelliPrefs *prefs, uint64_t now)
{
    JelliPrefsNetwork found[8];
    size_t count = next() % 9u;
    for (size_t i = 0u; i < count; ++i) {
        found[i] = (JelliPrefsNetwork){{0},
                                       (uint8_t)(next() % 33u),
                                       (int8_t)next(),
                                       (uint8_t)(next() % 4u ? 1u : next() % 3u)};
        memset(found[i].ssid, (int)('a' + i), found[i].ssid_size);
    }
    jelli_prefs_scanned(prefs, found, count, next() % 8u != 0u, now);
}

static void invariants(const JelliPrefs *prefs, const Typed *typed)
{
    CHECK(!prefs->reveal || prefs->screen == JELLI_PREFS_PASSWORD);
    if (prefs->screen != JELLI_PREFS_PASSWORD)
        CHECK(wiped(prefs) && typed->size == 0u);
    else
        CHECK(prefs->password_size == typed->size &&
              !memcmp(prefs->password, typed->password, sizeof(prefs->password)));
    if (prefs->screen == JELLI_PREFS_CLOSED)
        CHECK(prefs->count == 0u && prefs->current_size == 0u && !prefs->connected &&
              zero(prefs->networks, 0u, sizeof(prefs->networks)) &&
              zero(prefs->current, 0u, sizeof(prefs->current)));
}

/* The side button: open Settings, or close it. */
static void button(JelliPrefs *prefs, Typed *typed, uint64_t now)
{
    static const uint8_t home[] = "Home";
    if (prefs->screen != JELLI_PREFS_CLOSED) {
        reopened_mid_typing += typed->size != 0u;
        jelli_prefs_close(prefs);
    } else {
        jelli_prefs_open(prefs, home, next() % 5u, next() & 1u, now);
    }
    forget(typed);
}

static void one_step(JelliPrefs *prefs, JelliPetScreen *power, Typed *typed, uint64_t now)
{
    int x = 0, y = 0;
    switch (next() % 64u) {
    case 0:
        jelli_pet_screen_activity(power, now);
        button(prefs, typed, now);
        break;
    case 1:
        jelli_prefs_open(prefs, NULL, 0u, false, now); /* Reopened from the console. */
        reopened_mid_typing += typed->size != 0u;
        forget(typed);
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        scan(prefs, now);
        break;
    case 6:
    case 7:
    case 8:
        jelli_prefs_joined(prefs, (JelliPrefsOutcome)(next() % 5u), now);
        break;
    case 9:
    case 10:
        (void)jelli_prefs_tick(prefs, now);
        break;
    case 11:
    case 12:
        jelli_pet_screen_activity(power, now);
        jelli_prefs_drag(prefs, (int)(next() % 200u) - 100);
        break;
    default:
        jelli_pet_screen_activity(power, now);
        aim(prefs, &x, &y);
        if (prefs->screen == JELLI_PREFS_PASSWORD)
            tap_password(prefs, typed, x, y, now);
        else
            tap_elsewhere(prefs, x, y, now);
        break;
    }
}

static void run(void)
{
    JelliPrefs prefs;
    JelliPetScreen power;
    Typed typed;
    uint64_t now = 0u;
    state = 0xbb67ae85u ^ (seed * 2654435761u) ^ 1u;
    forget(&typed);
    jelli_prefs_init(&prefs);
    jelli_pet_screen_init_idle(&power, now, IDLE_MS);
    for (step = 0u; step < STEPS; ++step) {
        now += next() % 12u == 0u ? next() % 150000u : next() % 1500u;
        if (jelli_pet_screen_idle(&power, now)) { /* Dark: Settings closes with it. */
            darkened_mid_typing += typed.size != 0u;
            jelli_prefs_close(&prefs);
            forget(&typed);
        }
        if (power.asleep) { /* Only a double tap or the side button wakes it. */
            if (next() % 3u == 0u && !jelli_pet_screen_wake_tap(&power, 40, 40, now))
                CHECK(jelli_pet_screen_wake_tap(&power, 44, 41, now + 100u));
            CHECK(prefs.screen == JELLI_PREFS_CLOSED);
        } else {
            one_step(&prefs, &power, &typed, now);
        }
        invariants(&prefs, &typed);
    }
}

int main(void)
{
    for (seed = 0u; seed < SEEDS; ++seed)
        run();
    step = 0u;
    /* Every way out was taken with a password half typed. */
    CHECK(joins_with_password > 30u && darkened_mid_typing > 30u && cancels > 100u &&
          reopened_mid_typing > 50u);
    printf("Settings typing model passed (%u seeds of %u steps; %u joins with a password, %u "
           "darkened mid-typing)\n",
           (unsigned)SEEDS, (unsigned)STEPS, joins_with_password, darkened_mid_typing);
    return 0;
}

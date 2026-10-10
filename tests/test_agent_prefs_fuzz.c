#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Property: any mix of taps, drags, scan and join results, model lists, ticks, and
 * closes keeps Settings consistent, and a password is never held outside the password
 * screen. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: seed %u step %u: %s\n", __FILE__, __LINE__, seed, step,        \
                    #expr);                                                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned seed, step, picks;
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

/* Mostly lists loki/1 allows; sometimes a name, count, or current it does not. */
static void random_models(JelliPrefs *prefs)
{
    JelliPrefsModels models;
    memset(&models, 0, sizeof(models));
    models.count = (uint8_t)(next() % 19u);
    models.current = (uint8_t)(next() % 8u ? (models.count ? next() % models.count : 0u) : next());
    for (unsigned i = 0u; i < models.count && i < JELLI_PREFS_MAX_MODELS; ++i) {
        size_t length = next() % 8u ? 1u + next() % 40u : next() % 42u;
        for (size_t c = 0u; c < length && c < JELLI_PREFS_MODEL_MAX + 1u; ++c)
            models.names[i][c] = next() % 400u ? (char)('a' + next() % 26u) : (char)next();
    }
    bool online = next() & 1u;
    JelliPrefs before = *prefs;
    bool taken = jelli_prefs_models(prefs, next() % 16u ? &models : NULL, online);
    CHECK(taken || !memcmp(&before.models, &prefs->models, sizeof(before.models)));
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

/* The length of a model name within its buffer; one past the buffer when unterminated. */
static size_t strnlen_bound(const char *name)
{
    const char *end = memchr(name, '\0', JELLI_PREFS_MODEL_MAX + 1u);
    return end ? (size_t)(end - name) : JELLI_PREFS_MODEL_MAX + 1u;
}

/* The models: a list loki/1 allows, shown only when there is one, and none while closed. */
static void model_invariants(const JelliPrefs *prefs, JelliPrefsAction action)
{
    picks += action == JELLI_PREFS_DO_SET_MODEL;
    const JelliPrefsModels *models = &prefs->models;
    CHECK(models->count <= JELLI_PREFS_MAX_MODELS);
    CHECK(models->count ? models->current < models->count : models->current == 0u);
    CHECK(prefs->model_top == 0u || prefs->model_top + PREFS_LIST_ROWS <= models->count);
    for (unsigned i = 0u; i < JELLI_PREFS_MAX_MODELS; ++i) {
        size_t length = strnlen_bound(models->names[i]);
        CHECK(i < models->count ? length >= 1u && length <= JELLI_PREFS_MODEL_MAX : length == 0u);
    }
    if (prefs->screen == JELLI_PREFS_MODELS || action == JELLI_PREFS_DO_SET_MODEL)
        CHECK(prefs->screen == JELLI_PREFS_MODELS && models->count && jelli_prefs_model(prefs));
    if (prefs->screen == JELLI_PREFS_CLOSED)
        CHECK(!models->count && !prefs->bridge_online);
}

static void invariants(const JelliPrefs *prefs, const JelliPrefsJoin *join, JelliPrefsAction action)
{
    CHECK(prefs->screen <= JELLI_PREFS_MODELS);
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
    model_invariants(prefs, action);
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
        switch (next() % 13u) {
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
        case 6:
            random_models(&prefs);
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
    seed = step = 0u;
    CHECK(picks > 400u); /* Models were picked, not only listed. */
    printf("Settings fuzz checks passed (300 seeds of 4000 steps, %u models picked)\n", picks);
    return 0;
}

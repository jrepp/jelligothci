#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Property test of the model list in Settings (ADR-018) against a reference written
 * from the ADR's words: seeded runs of lists (valid, empty, and ones loki/1 forbids),
 * taps anywhere but the Wi-Fi card, drags, Back, Done, and reopening. After every step
 * the screen, list, default, and first row shown match the reference; the first row is
 * never past the last page; the default is always in the list; a pick names the row
 * tapped of the list last shown; and a refused list changes nothing at all. A failure
 * prints its seed and step to replay. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: seed %u step %u: %s\n", __FILE__, __LINE__, seed, step,        \
                    #expr);                                                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SEEDS = 600u, STEPS = 500u };

static const char ALLOWED[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._:/+-";
static unsigned seed, step, picks, refusals, scrolls;
static uint32_t state;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/* What the ADR says Settings shows. */
typedef struct {
    JelliPrefsScreen screen;
    JelliPrefsModels models;
    unsigned top;
    int drag;
    bool online;
} Reference;

static unsigned last_page(const Reference *ref)
{
    return ref->models.count > 5u ? ref->models.count - 5u : 0u;
}

static void random_list(JelliPrefsModels *models)
{
    memset(models, 0, sizeof(*models));
    models->count = (uint8_t)(next() % 8u ? 1u + next() % 16u : next() % 3u);
    for (unsigned i = 0u; i < models->count; ++i) {
        size_t length = next() % 4u ? 1u + next() % 12u : 35u + next() % 6u;
        for (size_t c = 0u; c < length; ++c)
            models->names[i][c] = ALLOWED[next() % (sizeof(ALLOWED) - 1u)];
    }
    models->current = (uint8_t)(models->count ? next() % models->count : 0u);
}

/* A list loki/1 forbids: too long, current past it, a bad name, or one unterminated. */
static void broken_list(JelliPrefsModels *models)
{
    random_list(models);
    if (!models->count)
        models->count = 1u;
    switch (next() % 4u) {
    case 0:
        models->count = (uint8_t)(17u + next() % 200u);
        break;
    case 1:
        models->current = (uint8_t)(models->count + next() % 10u);
        break;
    case 2:
        models->names[next() % models->count][0] = " \"\\,*\x7f\xc3"[next() % 7u];
        break;
    default:
        memset(models->names[next() % models->count], 'a', JELLI_PREFS_MODEL_MAX + 1u);
        break;
    }
}

static void offer(JelliPrefs *prefs, Reference *ref)
{
    JelliPrefsModels models;
    bool online = next() & 1u;
    if (next() % 10u == 0u) {
        broken_list(&models);
        static JelliPrefs before;
        memcpy(&before, prefs, sizeof(before));
        CHECK(!jelli_prefs_models(prefs, &models, online));
        /* before is a byte copy of *prefs, so even padding matches unless written, and
         * a write of any byte is what this must catch. */
        /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
        CHECK(!memcmp(&before, prefs, sizeof(before)));
        ++refusals;
        return;
    }
    random_list(&models);
    bool none = next() % 12u == 0u;
    CHECK(jelli_prefs_models(prefs, none ? NULL : &models, online));
    if (ref->screen == JELLI_PREFS_CLOSED)
        return; /* Ignored: opening asks again. */
    memset(&ref->models, 0, sizeof(ref->models));
    if (!none && models.count)
        ref->models = models;
    ref->online = online;
    if (ref->top > last_page(ref))
        ref->top = last_page(ref);
    if (ref->screen == JELLI_PREFS_MODELS && !ref->models.count) {
        ref->screen = JELLI_PREFS_MENU;
        ref->drag = 0;
    }
}

/* A row per 48 px of finger, and no drag kept at either end. */
static void drag(JelliPrefs *prefs, Reference *ref)
{
    int dy = next() % 20u ? (int)(next() % 401u) - 200 : (next() & 1u ? 5000 : -5000);
    unsigned before = prefs->model_top;
    jelli_prefs_drag(prefs, dy);
    if (ref->screen != JELLI_PREFS_MODELS || dy < -4096 || dy > 4096)
        return;
    ref->drag += dy;
    while (ref->drag >= 48 || ref->drag <= -48) {
        bool up = ref->drag > 0;
        if (up ? ref->top == 0u : ref->top >= last_page(ref)) {
            ref->drag = 0;
            break;
        }
        ref->top = up ? ref->top - 1u : ref->top + 1u;
        ref->drag += up ? -48 : 48;
    }
    scrolls += prefs->model_top != before;
}

/* A point in the part, a row, or anywhere but the Wi-Fi card (which leaves this test). */
static void random_point(int *x, int *y)
{
    unsigned roll = next() % 4u;
    PrefsBox box = roll == 0u   ? prefs_box(PREFS_MENU_MODEL)
                   : roll == 1u ? prefs_box(next() & 1u ? PREFS_MODELS_BACK : PREFS_MENU_DONE)
                   : roll == 2u ? prefs_row_box(next() % 5u)
                                : (PrefsBox){0, 0, 466, 466};
    do {
        *x = box.x + (int)(next() % (unsigned)box.width);
        *y = box.y + (int)(next() % (unsigned)box.height);
    } while (prefs_inside(prefs_box(PREFS_MENU_WIFI), *x, *y));
}

static void tap_menu(Reference *ref, int x, int y)
{
    if (prefs_inside(prefs_box(PREFS_MENU_MODEL), x, y) && ref->models.count) {
        unsigned top = ref->models.current > 2u ? ref->models.current - 2u : 0u;
        ref->top = top < last_page(ref) ? top : last_page(ref);
        ref->screen = JELLI_PREFS_MODELS;
        ref->drag = 0;
    } else if (prefs_inside(prefs_box(PREFS_MENU_DONE), x, y)) {
        memset(ref, 0, sizeof(*ref));
        ref->screen = JELLI_PREFS_CLOSED;
    }
}

/* The model a tap on the list picks, if any (-1 for Back, -2 for nothing). */
static int tapped_model(const Reference *ref, int x, int y)
{
    if (prefs_inside(prefs_box(PREFS_MODELS_BACK), x, y))
        return -1;
    for (unsigned row = 0u; row < 5u; ++row) {
        PrefsBox box = {63, 102 + 48 * (int)row, 340, 44};
        if (prefs_inside(box, x, y) && ref->top + row < ref->models.count)
            return (int)(ref->top + row);
    }
    return -2;
}

static void tap(JelliPrefs *prefs, Reference *ref)
{
    int x, y;
    random_point(&x, &y);
    JelliPrefsJoin join;
    JelliPrefsAction action = jelli_prefs_tap(prefs, x, y, step, &join);
    JelliPrefsAction expected = JELLI_PREFS_DO_NOTHING;
    if (ref->screen == JELLI_PREFS_MENU) {
        tap_menu(ref, x, y);
        expected = ref->screen == JELLI_PREFS_CLOSED ? JELLI_PREFS_DO_CLOSE : expected;
    } else if (ref->screen == JELLI_PREFS_MODELS) {
        int index = tapped_model(ref, x, y);
        if (index == -1) {
            ref->screen = JELLI_PREFS_MENU;
            ref->drag = 0;
        } else if (index >= 0 && (unsigned)index != ref->models.current) {
            ref->models.current = (uint8_t)index;
            expected = JELLI_PREFS_DO_SET_MODEL;
        }
    }
    CHECK(action == expected);
    if (action == JELLI_PREFS_DO_SET_MODEL) { /* The name of the row tapped, as shown. */
        const char *picked = jelli_prefs_model(prefs);
        CHECK(picked && !strcmp(picked, ref->models.names[ref->models.current]));
        CHECK(ref->models.current >= ref->top && ref->models.current < ref->top + 5u);
        ++picks;
    }
}

static void agree(const JelliPrefs *prefs, const Reference *ref)
{
    CHECK(prefs->screen == ref->screen && prefs->bridge_online == ref->online);
    CHECK(prefs->models.count == ref->models.count);
    CHECK(prefs->models.count <= JELLI_PREFS_MAX_MODELS);
    CHECK(!memcmp(&prefs->models, &ref->models, sizeof(ref->models)));
    CHECK(!prefs->models.count || prefs->models.current < prefs->models.count);
    if (prefs->screen == JELLI_PREFS_MODELS) {
        CHECK(prefs->model_top == ref->top && prefs->model_top <= last_page(ref));
        CHECK(prefs->drag == ref->drag && prefs->models.count);
    }
    const char *name = jelli_prefs_model(prefs);
    CHECK(prefs->models.count ? name == prefs->models.names[prefs->models.current] : !name);
}

static void run(void)
{
    JelliPrefs prefs;
    Reference ref;
    memset(&ref, 0, sizeof(ref));
    state = 0x9e3779b9u ^ (seed * 0x85ebca6bu) ^ 1u;
    jelli_prefs_init(&prefs);
    for (step = 0u; step < STEPS; ++step) {
        unsigned roll = next() % 10u;
        if (roll == 0u || ref.screen == JELLI_PREFS_CLOSED) {
            jelli_prefs_open(&prefs, NULL, 0u, next() & 1u, step);
            memset(&ref, 0, sizeof(ref));
            ref.screen = JELLI_PREFS_MENU;
        } else if (roll < 4u) {
            offer(&prefs, &ref);
        } else if (roll < 6u) {
            drag(&prefs, &ref);
        } else {
            tap(&prefs, &ref);
        }
        agree(&prefs, &ref);
    }
}

int main(void)
{
    for (seed = 0u; seed < SEEDS; ++seed)
        run();
    seed = step = 0u;
    CHECK(picks > 5000u && refusals > 5000u && scrolls > 3000u);
    printf("Settings model property checks passed (%u seeds of %u steps; %u picks, %u lists "
           "refused, %u scrolls)\n",
           (unsigned)SEEDS, (unsigned)STEPS, picks, refusals, scrolls);
    return 0;
}

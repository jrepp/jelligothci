#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The default model in Settings (ADR-018): the Model card, the list the bridge offers,
 * picking one, scrolling, and lists that break loki/1's rules or arrive at any time. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static bool on_screen(int x, int y)
{
    int64_t dx = (int64_t)x - PREFS_CENTER, dy = (int64_t)y - PREFS_CENTER;
    return dx * dx + dy * dy <= (int64_t)(PREFS_CENTER - 1) * (PREFS_CENTER - 1);
}

static bool box_on_screen(PrefsBox box, int corner)
{
    int left = box.x + corner, right = box.x + box.width - 1 - corner;
    int top = box.y + corner, bottom = box.y + box.height - 1 - corner;
    return on_screen(left, top) && on_screen(right, top) && on_screen(left, bottom) &&
           on_screen(right, bottom);
}

static bool apart(PrefsBox a, PrefsBox b)
{
    return a.x >= b.x + b.width || b.x >= a.x + a.width || a.y >= b.y + b.height ||
           b.y >= a.y + a.height;
}

/* A list of count models named "m0", "m1", ..., the first "Claude", current in use. */
static JelliPrefsModels offer(unsigned count, unsigned current)
{
    JelliPrefsModels models;
    memset(&models, 0, sizeof(models));
    for (unsigned i = 0u; i < count; ++i)
        if (i)
            (void)snprintf(models.names[i], sizeof(models.names[i]), "m%u:x", i);
        else
            memcpy(models.names[i], "Claude", 7u);
    models.count = (uint8_t)count;
    models.current = (uint8_t)current;
    return models;
}

static JelliPrefsAction tap_box(JelliPrefs *prefs, PrefsBox box)
{
    JelliPrefsJoin join;
    return jelli_prefs_tap(prefs, box.x + box.width / 2, box.y + box.height / 2, 0u, &join);
}

static void opened(JelliPrefs *prefs, const JelliPrefsModels *models, bool online)
{
    jelli_prefs_init(prefs);
    jelli_prefs_open(prefs, NULL, 0u, false, 0u);
    CHECK(jelli_prefs_models(prefs, models, online));
}

/* Three cards apart, inside the circle, at least a finger's size; Back below the rows. */
static void test_layout(void)
{
    static const PrefsPart menu[] = {PREFS_MENU_WIFI, PREFS_MENU_MODEL, PREFS_MENU_DONE};
    for (unsigned i = 0u; i < 3u; ++i) {
        PrefsBox box = prefs_box(menu[i]);
        CHECK(box.width >= 37 && box.height >= 42 && box_on_screen(box, 3));
        for (unsigned j = i + 1u; j < 3u; ++j)
            CHECK(apart(box, prefs_box(menu[j])));
    }
    CHECK(prefs_box(PREFS_MENU_WIFI).y >= 58 + 36 + 8); /* Below the title. */
    PrefsBox back = prefs_box(PREFS_MODELS_BACK);
    CHECK(back.width >= 37 && back.height >= 42 && box_on_screen(back, 3));
    for (unsigned row = 0u; row < PREFS_LIST_ROWS; ++row)
        CHECK(apart(prefs_row_box(row), back));
}

/* Without a list the card opens nothing, online or not. */
static void test_no_list(void)
{
    JelliPrefs prefs;
    for (unsigned online = 0u; online < 2u; ++online) {
        opened(&prefs, NULL, online != 0u);
        CHECK(prefs.bridge_online == (online != 0u) && !jelli_prefs_model(&prefs));
        CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_MODEL)) == JELLI_PREFS_DO_NOTHING);
        CHECK(prefs.screen == JELLI_PREFS_MENU);
        JelliPrefsModels none = offer(0u, 0u);
        CHECK(jelli_prefs_models(&prefs, &none, true) && !jelli_prefs_model(&prefs));
        CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_MODEL)) == JELLI_PREFS_DO_NOTHING);
        CHECK(prefs.screen == JELLI_PREFS_MENU);
    }
}

/* A tap picks a model at once and asks for set_model; the bridge's list then rules. */
static void test_pick(void)
{
    JelliPrefs prefs;
    JelliPrefsModels models = offer(2u, 0u);
    opened(&prefs, &models, true);
    CHECK(!strcmp(jelli_prefs_model(&prefs), "Claude"));
    CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_MODEL)) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_MODELS && prefs.model_top == 0u && prefs.dirty);
    CHECK(tap_box(&prefs, prefs_row_box(0u)) == JELLI_PREFS_DO_NOTHING); /* Already. */
    CHECK(tap_box(&prefs, prefs_row_box(2u)) == JELLI_PREFS_DO_NOTHING); /* No row. */
    prefs.dirty = false;
    CHECK(tap_box(&prefs, prefs_row_box(1u)) == JELLI_PREFS_DO_SET_MODEL);
    CHECK(prefs.dirty && prefs.screen == JELLI_PREFS_MODELS);
    CHECK(!strcmp(jelli_prefs_model(&prefs), "m1:x") && prefs.models.current == 1u);
    CHECK(jelli_prefs_models(&prefs, &models, true)); /* The bridge kept Claude. */
    CHECK(!strcmp(jelli_prefs_model(&prefs), "Claude") && prefs.screen == JELLI_PREFS_MODELS);
    CHECK(tap_box(&prefs, prefs_box(PREFS_MODELS_BACK)) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_MENU);
    CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_DONE)) == JELLI_PREFS_DO_CLOSE);
    CHECK(prefs.models.count == 0u && !prefs.bridge_online && !jelli_prefs_model(&prefs));
    const char *bytes = (const char *)prefs.models.names;
    const size_t size = sizeof(prefs.models.names);
    for (size_t i = 0u; i < size; ++i)
        CHECK(bytes[i] == '\0');
}

/* Lists that break loki/1's rules change nothing; while closed nothing is kept. */
static void test_refused(void)
{
    JelliPrefs prefs;
    JelliPrefsModels good = offer(3u, 2u), bad;
    opened(&prefs, &good, true);
    JelliPrefs before = prefs;
    const char *const NAMES[] = {"", "a b", "a\"b", "g\xc3\xa9", "a,b", "a\nb", "a\\b"};
    for (unsigned i = 0u; i < sizeof(NAMES) / sizeof(NAMES[0]) + 3u; ++i) {
        bad = good;
        if (i < sizeof(NAMES) / sizeof(NAMES[0]))
            (void)snprintf(bad.names[1], sizeof(bad.names[1]), "%s", NAMES[i]);
        else if (i == sizeof(NAMES) / sizeof(NAMES[0]))
            memset(bad.names[2], 'a', sizeof(bad.names[2])); /* No NUL. */
        else
            bad.current = (uint8_t)(i % 2u ? 3u : 200u);
        CHECK(!jelli_prefs_models(&prefs, &bad, false));
        CHECK(!memcmp(&prefs.models, &before.models, sizeof(prefs.models)));
        CHECK(prefs.bridge_online == before.bridge_online);
    }
    bad = offer(16u, 0u);
    bad.count = 17u;
    CHECK(!jelli_prefs_models(&prefs, &bad, true) && prefs.models.count == 3u);
    CHECK(!jelli_prefs_models(NULL, &good, true));
    jelli_prefs_close(&prefs);
    CHECK(jelli_prefs_models(&prefs, &good, true) && prefs.models.count == 0u);
    CHECK(prefs.screen == JELLI_PREFS_CLOSED && !prefs.dirty && !jelli_prefs_model(NULL));
}

/* The default opens in view; drags move a row at a time and stop at either end; a
 * row tapped while scrolled is the model it shows. */
static void test_scrolling(void)
{
    JelliPrefs prefs;
    for (unsigned count = 1u; count <= JELLI_PREFS_MAX_MODELS; ++count)
        for (unsigned current = 0u; current < count; ++current) {
            JelliPrefsModels models = offer(count, current);
            opened(&prefs, &models, true);
            (void)tap_box(&prefs, prefs_box(PREFS_MENU_MODEL));
            unsigned top = prefs.model_top, last = count > 5u ? count - 5u : 0u;
            CHECK(prefs.screen == JELLI_PREFS_MODELS && top <= last);
            CHECK(current >= top && current < top + PREFS_LIST_ROWS);
            jelli_prefs_drag(&prefs, PREFS_ROW_PITCH * 40);
            CHECK(prefs.model_top == 0u && prefs.drag == 0);
            jelli_prefs_drag(&prefs, -PREFS_ROW_PITCH * 40);
            CHECK(prefs.model_top == last && prefs.drag == 0);
            unsigned pick = (current + 1u) % count; /* Another model, when it is shown. */
            if (pick != current && pick >= last) {
                CHECK(tap_box(&prefs, prefs_row_box(pick - last)) == JELLI_PREFS_DO_SET_MODEL);
                CHECK(prefs.models.current == pick);
            }
        }
    JelliPrefsModels models = offer(16u, 15u);
    opened(&prefs, &models, true);
    (void)tap_box(&prefs, prefs_box(PREFS_MENU_MODEL));
    CHECK(prefs.model_top == 11u);
    jelli_prefs_drag(&prefs, PREFS_ROW_PITCH - 1);
    CHECK(prefs.model_top == 11u && prefs.drag == PREFS_ROW_PITCH - 1);
    jelli_prefs_drag(&prefs, 1);
    CHECK(prefs.model_top == 10u && prefs.drag == 0);
    jelli_prefs_drag(&prefs, 4097); /* Out of range: ignored. */
    CHECK(prefs.model_top == 10u);
    prefs.screen = JELLI_PREFS_MENU; /* Drags scroll only a list that is shown. */
    jelli_prefs_drag(&prefs, PREFS_ROW_PITCH * 3);
    CHECK(prefs.model_top == 10u);
}

/* A list that changes while shown: a shorter one keeps a full page, none goes back. */
static void test_list_changes_while_shown(void)
{
    JelliPrefs prefs;
    JelliPrefsModels models = offer(16u, 15u);
    opened(&prefs, &models, true);
    (void)tap_box(&prefs, prefs_box(PREFS_MENU_MODEL));
    models = offer(7u, 1u);
    CHECK(jelli_prefs_models(&prefs, &models, true));
    CHECK(prefs.screen == JELLI_PREFS_MODELS && prefs.model_top == 2u && prefs.dirty);
    models = offer(3u, 1u);
    CHECK(jelli_prefs_models(&prefs, &models, true) && prefs.model_top == 0u);
    CHECK(jelli_prefs_models(&prefs, NULL, false));
    CHECK(prefs.screen == JELLI_PREFS_MENU && !prefs.bridge_online && !prefs.models.count);
    JelliPrefsJoin join;
    CHECK(jelli_prefs_tap(&prefs, 233, 240, 0u, &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_MENU); /* The card opens nothing now. */
}

int main(void)
{
    test_layout();
    test_no_list();
    test_pick();
    test_refused();
    test_scrolling();
    test_list_changes_while_shown();
    printf("Settings model choice checks passed\n");
    return 0;
}

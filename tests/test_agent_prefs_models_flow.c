#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The Model card and list in Settings at their edges (ADR-018), white-box: the taps
 * docs/hardware.md gives, every row at every scroll position and the gaps between them,
 * the default in view on opening, an optimistic pick the bridge confirms or corrects,
 * lists that change while shown, and the Wi-Fi flows around the re-laid menu, which a
 * list arriving at any moment must not disturb. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: case %u: %s\n", __FILE__, __LINE__, current_case, #expr);      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned current_case;

/* count models named "n0", "n1", ... (or with prefix), current in use. */
static JelliPrefsModels offer(unsigned count, unsigned current, const char *prefix)
{
    JelliPrefsModels models;
    memset(&models, 0, sizeof(models));
    for (unsigned i = 0u; i < count; ++i)
        (void)snprintf(models.names[i], sizeof(models.names[i]), "%s%u", prefix, i);
    models.count = (uint8_t)count;
    models.current = (uint8_t)current;
    return models;
}

static JelliPrefsAction tap_at(JelliPrefs *prefs, int x, int y)
{
    JelliPrefsJoin join;
    return jelli_prefs_tap(prefs, x, y, 0u, &join);
}

static JelliPrefsAction tap_box(JelliPrefs *prefs, PrefsBox box)
{
    return tap_at(prefs, box.x + box.width / 2, box.y + box.height / 2);
}

static void opened(JelliPrefs *prefs, unsigned count, unsigned current)
{
    JelliPrefsModels models = offer(count, current, "n");
    jelli_prefs_init(prefs);
    jelli_prefs_open(prefs, (const uint8_t *)"Home", 4u, true, 0u);
    CHECK(jelli_prefs_models(prefs, count ? &models : NULL, true));
}

/* The first row shown on opening: the default in the middle when the list allows. */
static unsigned centred(unsigned count, unsigned current)
{
    unsigned last = count > 5u ? count - 5u : 0u, top = current > 2u ? current - 2u : 0u;
    return top < last ? top : last;
}

/* docs/hardware.md's taps reach the cards they name; the gap between cards reaches none. */
static void test_documented_taps(void)
{
    JelliPrefs prefs;
    opened(&prefs, 3u, 1u);
    CHECK(tap_at(&prefs, 233, 188) == JELLI_PREFS_DO_SCAN && prefs.screen == JELLI_PREFS_SCANNING);
    opened(&prefs, 3u, 1u);
    CHECK(tap_at(&prefs, 233, 244) == JELLI_PREFS_DO_NOTHING && prefs.screen == JELLI_PREFS_MODELS);
    PrefsBox wifi = prefs_box(PREFS_MENU_WIFI), model = prefs_box(PREFS_MENU_MODEL);
    CHECK(model.y - (wifi.y + wifi.height) >= 8); /* Apart enough for a finger. */
    for (int y = wifi.y + wifi.height; y < model.y; ++y) {
        current_case = (unsigned)y;
        opened(&prefs, 3u, 1u);
        CHECK(tap_at(&prefs, 233, y) == JELLI_PREFS_DO_NOTHING && prefs.screen == JELLI_PREFS_MENU);
    }
    opened(&prefs, 3u, 1u); /* The edges of the Model card are the card. */
    CHECK(tap_at(&prefs, model.x, model.y) == JELLI_PREFS_DO_NOTHING &&
          prefs.screen == JELLI_PREFS_MODELS);
    opened(&prefs, 3u, 1u);
    CHECK(tap_at(&prefs, model.x + model.width - 1, model.y + model.height - 1) ==
              JELLI_PREFS_DO_NOTHING &&
          prefs.screen == JELLI_PREFS_MODELS);
    opened(&prefs, 3u, 1u);
    CHECK(tap_at(&prefs, model.x + model.width, model.y) == JELLI_PREFS_DO_NOTHING &&
          prefs.screen == JELLI_PREFS_MENU);
    CHECK(tap_at(&prefs, wifi.x, wifi.y + wifi.height - 1) == JELLI_PREFS_DO_SCAN);
}

/* The card's states: a list comes only while open; the card opens only with one; a
 * close forgets it and a fresh open waits for the host to offer it again. */
static void test_card_states(void)
{
    JelliPrefs prefs;
    JelliPrefsModels models = offer(4u, 2u, "n");
    current_case = 0u;
    jelli_prefs_init(&prefs);
    CHECK(jelli_prefs_models(&prefs, &models, true) && !jelli_prefs_model(&prefs)); /* Closed. */
    CHECK(!prefs.bridge_online && prefs.screen == JELLI_PREFS_CLOSED && !prefs.dirty);
    jelli_prefs_open(&prefs, NULL, 0u, false, 5u);
    CHECK(!jelli_prefs_model(&prefs) && !prefs.bridge_online);            /* Bridge offline. */
    CHECK(jelli_prefs_models(&prefs, NULL, true) && prefs.bridge_online); /* An older bridge. */
    CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_MODEL)) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_MENU);
    prefs.dirty = false;
    CHECK(jelli_prefs_models(&prefs, &models, true) && prefs.dirty);
    CHECK(!strcmp(jelli_prefs_model(&prefs), "n2") && prefs.screen == JELLI_PREFS_MENU);
    CHECK(jelli_prefs_models(&prefs, &models, false) && !prefs.bridge_online); /* Kept. */
    CHECK(!strcmp(jelli_prefs_model(&prefs), "n2"));
    jelli_prefs_open(&prefs, NULL, 0u, false, 9u); /* Opening again starts from nothing. */
    CHECK(!jelli_prefs_model(&prefs) && !prefs.models.count && !prefs.bridge_online);
    CHECK(prefs.model_top == 0u && prefs.screen == JELLI_PREFS_MENU);
    CHECK(!jelli_prefs_model(NULL));
    prefs.models.count = 2u; /* A corrupt current is never named. */
    prefs.models.current = 2u;
    CHECK(!jelli_prefs_model(&prefs));
}

/* Opening puts the default in view, centred where it can be, from a clean drag. */
static void test_opening_centres_the_default(void)
{
    JelliPrefs prefs;
    for (unsigned count = 1u; count <= JELLI_PREFS_MAX_MODELS; ++count)
        for (unsigned current = 0u; current < count; ++current) {
            current_case = count * 100u + current;
            opened(&prefs, count, current);
            prefs.drag = 40; /* Left over from elsewhere. */
            prefs.model_top = 13u;
            prefs.dirty = false;
            CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_MODEL)) == JELLI_PREFS_DO_NOTHING);
            CHECK(prefs.screen == JELLI_PREFS_MODELS && prefs.dirty && prefs.drag == 0);
            CHECK(prefs.model_top == centred(count, current));
        }
}

/* Every row at every scroll position picks the model it shows, from its very edges; the
 * gaps between rows and the sides beyond them pick nothing. */
static void test_every_row_everywhere(void)
{
    JelliPrefs prefs;
    for (unsigned top = 0u; top <= 11u; ++top)
        for (unsigned row = 0u; row < PREFS_LIST_ROWS; ++row) {
            current_case = top * 10u + row;
            PrefsBox box = prefs_row_box(row);
            const int xs[] = {box.x, box.x + box.width - 1, box.x + box.width / 2};
            const int ys[] = {box.y, box.y + box.height - 1, box.y + box.height / 2};
            for (unsigned corner = 0u; corner < 3u; ++corner) {
                opened(&prefs, 16u, 0u);
                (void)tap_box(&prefs, prefs_box(PREFS_MENU_MODEL));
                jelli_prefs_drag(&prefs, -(int)top * PREFS_ROW_PITCH);
                CHECK(prefs.model_top == top);
                unsigned index = top + row;
                JelliPrefsAction action = tap_at(&prefs, xs[corner], ys[corner]);
                CHECK(action == (index ? JELLI_PREFS_DO_SET_MODEL : JELLI_PREFS_DO_NOTHING));
                char name[8];
                (void)snprintf(name, sizeof(name), "n%u", index);
                CHECK(!strcmp(jelli_prefs_model(&prefs), name) && prefs.model_top == top);
                CHECK(tap_at(&prefs, xs[corner], ys[corner]) ==
                      JELLI_PREFS_DO_NOTHING); /* Again. */
            }
            opened(&prefs, 16u, 15u);
            (void)tap_box(&prefs, prefs_box(PREFS_MENU_MODEL));
            for (int y = box.y + box.height; y < box.y + PREFS_ROW_PITCH; ++y) /* The gap. */
                CHECK(tap_at(&prefs, box.x + 20, y) == JELLI_PREFS_DO_NOTHING);
            CHECK(tap_at(&prefs, box.x - 1, box.y + 5) == JELLI_PREFS_DO_NOTHING);
            CHECK(tap_at(&prefs, box.x + box.width, box.y + 5) == JELLI_PREFS_DO_NOTHING);
            CHECK(prefs.models.current == 15u && prefs.screen == JELLI_PREFS_MODELS);
        }
}

/* A pick shows at once; the bridge's next list confirms or corrects it, and a corrected
 * pick can be made again. Back and the card show the pick until then. */
static void test_pick_then_bridge(void)
{
    JelliPrefs prefs;
    JelliPrefsModels said = offer(8u, 0u, "n");
    current_case = 0u;
    opened(&prefs, 8u, 0u);
    (void)tap_box(&prefs, prefs_box(PREFS_MENU_MODEL));
    CHECK(tap_box(&prefs, prefs_row_box(3u)) == JELLI_PREFS_DO_SET_MODEL);
    CHECK(!strcmp(jelli_prefs_model(&prefs), "n3"));
    CHECK(tap_box(&prefs, prefs_box(PREFS_MODELS_BACK)) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_MENU && !strcmp(jelli_prefs_model(&prefs), "n3"));
    (void)tap_box(&prefs, prefs_box(PREFS_MENU_MODEL)); /* Opens around the pick. */
    CHECK(prefs.model_top == centred(8u, 3u));
    said.current = 3u; /* Confirmed. */
    CHECK(jelli_prefs_models(&prefs, &said, true) && !strcmp(jelli_prefs_model(&prefs), "n3"));
    said.current = 0u; /* Corrected: the bridge kept n0. */
    CHECK(jelli_prefs_models(&prefs, &said, true) && !strcmp(jelli_prefs_model(&prefs), "n0"));
    CHECK(prefs.screen == JELLI_PREFS_MODELS && prefs.model_top == centred(8u, 3u));
    CHECK(tap_box(&prefs, prefs_row_box(3u - prefs.model_top)) == JELLI_PREFS_DO_SET_MODEL);
    CHECK(!strcmp(jelli_prefs_model(&prefs), "n3"));
    JelliPrefsModels other = offer(8u, 5u, "o"); /* A new list without the pick. */
    CHECK(jelli_prefs_models(&prefs, &other, true) && !strcmp(jelli_prefs_model(&prefs), "o5"));
    for (unsigned i = 0u; i < other.count; ++i)
        CHECK(prefs.models.names[i][0] == 'o');
}

/* A list that changes while shown keeps a full page where it can, leaves the default
 * where the user put the page, and goes back to the menu once there is none. */
static void test_list_changes_while_open(void)
{
    static const struct {
        unsigned count, current, top;
    } STEPS[] = {{16u, 15u, 11u}, {6u, 5u, 1u},  {16u, 0u, 1u}, {2u, 1u, 0u},
                 {1u, 0u, 0u},    {16u, 9u, 0u}, {9u, 8u, 4u}};
    JelliPrefs prefs;
    opened(&prefs, 16u, 15u);
    (void)tap_box(&prefs, prefs_box(PREFS_MENU_MODEL));
    for (current_case = 0u; current_case < sizeof(STEPS) / sizeof(STEPS[0]); ++current_case) {
        JelliPrefsModels models =
            offer(STEPS[current_case].count, STEPS[current_case].current, "n");
        CHECK(jelli_prefs_models(&prefs, &models, true));
        CHECK(prefs.screen == JELLI_PREFS_MODELS && prefs.model_top == STEPS[current_case].top);
        PrefsBox past =
            prefs_row_box(STEPS[current_case].count < 5u ? STEPS[current_case].count : 4u);
        if (STEPS[current_case].count < 5u) /* Rows past the list are empty space. */
            CHECK(tap_box(&prefs, past) == JELLI_PREFS_DO_NOTHING);
        jelli_prefs_drag(&prefs, -PREFS_ROW_PITCH * 20);
        unsigned last = models.count > 5u ? models.count - 5u : 0u;
        CHECK(prefs.model_top == last && prefs.drag == 0);
    }
    prefs.drag = 30;
    JelliPrefsModels none = offer(0u, 0u, "n");
    CHECK(jelli_prefs_models(&prefs, &none, true));
    CHECK(prefs.screen == JELLI_PREFS_MENU && prefs.drag == 0 && !prefs.model_top);
    CHECK(prefs.bridge_online && !jelli_prefs_model(&prefs));
}

static void type_password(JelliPrefs *prefs, const char *text)
{
    for (const char *at = text; *at; ++at) {
        PrefsKey keys[PREFS_MAX_KEYS];
        size_t count = prefs_keys(prefs->page, keys, PREFS_MAX_KEYS);
        bool typed = false;
        for (size_t i = 0u; i < count && !typed; ++i)
            if (keys[i].kind == PREFS_KEY_CHAR && keys[i].ch == *at)
                typed = tap_box(prefs, keys[i].box) == JELLI_PREFS_DO_NOTHING;
        CHECK(typed);
    }
}

/* Lists arriving at every step of a Wi-Fi join change nothing of it: the scan, the
 * page of networks, the password typed, the join request, and the result. */
static void test_wifi_flows_undisturbed(void)
{
    JelliPrefs prefs;
    JelliPrefsJoin join;
    JelliPrefsNetwork found[7];
    memset(found, 0, sizeof(found));
    for (unsigned i = 0u; i < 7u; ++i) {
        found[i].ssid_size =
            (uint8_t)snprintf((char *)found[i].ssid, JELLI_PREFS_SSID_MAX, "w%u", i);
        found[i].rssi = (int8_t)(-40 - (int)i);
        found[i].security = JELLI_PREFS_PASSWORD_NETWORK;
    }
    JelliPrefsModels a = offer(16u, 7u, "n"), b = offer(3u, 0u, "o");
    current_case = 0u;
    opened(&prefs, 16u, 7u);
    CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_WIFI)) == JELLI_PREFS_DO_SCAN);
    CHECK(jelli_prefs_models(&prefs, NULL, false) && prefs.screen == JELLI_PREFS_SCANNING);
    jelli_prefs_scanned(&prefs, found, 7u, true, 10u);
    CHECK(prefs.screen == JELLI_PREFS_LIST && prefs.count == 7u);
    CHECK(jelli_prefs_models(&prefs, &a, true));
    prefs.model_top = 0u;
    jelli_prefs_drag(&prefs, -PREFS_ROW_PITCH * 2); /* The networks scroll, not the models. */
    CHECK(prefs.top == 2u && prefs.model_top == 0u);
    jelli_prefs_drag(&prefs, -20); /* Some drag in hand, then Back and the models list. */
    CHECK(tap_box(&prefs, prefs_box(PREFS_LIST_BACK)) == JELLI_PREFS_DO_NOTHING);
    CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_MODEL)) == JELLI_PREFS_DO_NOTHING);
    unsigned top = prefs.model_top;
    jelli_prefs_drag(&prefs, -(PREFS_ROW_PITCH - 20)); /* Not a row: the drag began afresh. */
    CHECK(prefs.model_top == top && prefs.top == 2u);
    CHECK(tap_box(&prefs, prefs_box(PREFS_MODELS_BACK)) == JELLI_PREFS_DO_NOTHING);
    CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_WIFI)) == JELLI_PREFS_DO_SCAN);
    jelli_prefs_scanned(&prefs, found, 7u, true, 20u);
    CHECK(tap_box(&prefs, prefs_row_box(1u)) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_PASSWORD && prefs.chosen == 1u);
    type_password(&prefs, "secretpw");
    CHECK(jelli_prefs_models(&prefs, &b, true) && jelli_prefs_models(&prefs, NULL, false));
    CHECK(prefs.screen == JELLI_PREFS_PASSWORD && prefs.password_size == 8u);
    CHECK(!memcmp(prefs.password, "secretpw", 8u));
    PrefsKey keys[PREFS_MAX_KEYS];
    size_t count = prefs_keys(prefs.page, keys, PREFS_MAX_KEYS);
    JelliPrefsAction action = JELLI_PREFS_DO_NOTHING;
    for (size_t i = 0u; i < count; ++i)
        if (keys[i].kind == PREFS_KEY_JOIN)
            action = jelli_prefs_tap(&prefs, keys[i].box.x + 5, keys[i].box.y + 5, 30u, &join);
    CHECK(action == JELLI_PREFS_DO_JOIN && join.password_size == 8u);
    CHECK(!memcmp(join.password, "secretpw", 8u) && join.ssid_size == 2u);
    CHECK(jelli_prefs_models(&prefs, &a, true) && prefs.screen == JELLI_PREFS_JOINING);
    jelli_prefs_joined(&prefs, JELLI_PREFS_JOINED, 40u);
    CHECK(jelli_prefs_models(&prefs, &b, true) && prefs.screen == JELLI_PREFS_RESULT);
    CHECK(jelli_prefs_tick(&prefs, 40u + JELLI_PREFS_RESULT_MS) &&
          prefs.screen == JELLI_PREFS_MENU);
    CHECK(!memcmp(prefs.current, "w1", 2u) && prefs.connected);
    CHECK(tap_box(&prefs, prefs_box(PREFS_MENU_MODEL)) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_MODELS && !strcmp(jelli_prefs_model(&prefs), "o0"));
}

int main(void)
{
    test_documented_taps();
    test_card_states();
    test_opening_centres_the_default();
    test_every_row_everywhere();
    test_pick_then_bridge();
    test_list_changes_while_open();
    test_wifi_flows_undisturbed();
    printf("Settings model flow checks passed\n");
    return 0;
}

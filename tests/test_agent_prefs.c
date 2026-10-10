#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Settings (ADR-016): its layout on the round screen, the keyboard, and the whole
 * Wi-Fi flow, with the password never kept longer than it is needed. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* A point is on the round screen when it lies inside its circle, with a margin. */
static bool on_screen(int x, int y)
{
    int64_t dx = (int64_t)x - PREFS_CENTER, dy = (int64_t)y - PREFS_CENTER;
    return dx * dx + dy * dy <= (int64_t)(PREFS_CENTER - 1) * (PREFS_CENTER - 1);
}

/* Every corner of a box, pulled in by the rounding its drawing gives it. */
static bool box_on_screen(PrefsBox box, int corner)
{
    int left = box.x + corner, right = box.x + box.width - 1 - corner;
    int top = box.y + corner, bottom = box.y + box.height - 1 - corner;
    return on_screen(left, top) && on_screen(right, top) && on_screen(left, bottom) &&
           on_screen(right, bottom);
}

static bool overlap(PrefsBox a, PrefsBox b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

static void test_layout_fits_the_round_screen(void)
{
    for (int part = 0; part < PREFS_PART_COUNT; ++part) {
        PrefsBox box = prefs_box((PrefsPart)part);
        CHECK(box.width >= 56 && box.height >= 42); /* A finger's size at least. */
        CHECK(box_on_screen(box, 3));
    }
    for (unsigned row = 0u; row < PREFS_LIST_ROWS; ++row)
        CHECK(box_on_screen(prefs_row_box(row), 3));
    CHECK(!overlap(prefs_row_box(PREFS_LIST_ROWS - 1u), prefs_box(PREFS_LIST_BACK)));
    CHECK(!overlap(prefs_box(PREFS_LIST_BACK), prefs_box(PREFS_LIST_SCAN)));
    CHECK(!overlap(prefs_box(PREFS_PASS_FIELD), prefs_box(PREFS_PASS_SHOW)));
    CHECK(!overlap(prefs_box(PREFS_RESULT_RETRY), prefs_box(PREFS_RESULT_BACK)));
    for (unsigned page = 0u; page < PREFS_PAGE_COUNT; ++page) {
        PrefsKey keys[PREFS_MAX_KEYS];
        size_t count = prefs_keys(page, keys, PREFS_MAX_KEYS);
        CHECK(count >= 20u && count <= PREFS_MAX_KEYS);
        for (size_t i = 0u; i < count; ++i) {
            CHECK(keys[i].box.width >= 37 && keys[i].box.height >= 42);
            CHECK(box_on_screen(keys[i].box, 3));
            CHECK(!overlap(keys[i].box, prefs_box(PREFS_PASS_FIELD)));
            CHECK(!overlap(keys[i].box, prefs_box(PREFS_PASS_CANCEL)));
            for (size_t j = i + 1u; j < count; ++j)
                CHECK(!overlap(keys[i].box, keys[j].box));
        }
    }
    CHECK(prefs_keys(PREFS_PAGE_COUNT, NULL, 0u) == 0u);
}

/* Every printable ASCII character a passphrase may hold is a key on some page. */
static void test_every_character_can_be_typed(void)
{
    for (int ch = ' '; ch <= '~'; ++ch) {
        bool found = false;
        for (unsigned page = 0u; page < PREFS_PAGE_COUNT && !found; ++page) {
            PrefsKey keys[PREFS_MAX_KEYS];
            size_t count = prefs_keys(page, keys, PREFS_MAX_KEYS);
            for (size_t i = 0u; i < count; ++i)
                found = found || keys[i].ch == (char)ch;
        }
        CHECK(found);
    }
}

static JelliPrefsNetwork network(const char *name, int rssi, JelliPrefsSecurity security)
{
    JelliPrefsNetwork result = {{0}, (uint8_t)strlen(name), (int8_t)rssi, (uint8_t)security};
    memcpy(result.ssid, name, strlen(name));
    return result;
}

static void tap_part(JelliPrefs *prefs, PrefsPart part, JelliPrefsAction expected, uint64_t now,
                     JelliPrefsJoin *join)
{
    PrefsBox box = prefs_box(part);
    CHECK(jelli_prefs_tap(prefs, box.x + box.width / 2, box.y + box.height / 2, now, join) ==
          expected);
}

/* Types text by tapping its keys; the page is set directly (page keys have their own
 * test), so each character's key is found wherever it is. */
static JelliPrefsAction type(JelliPrefs *prefs, const char *text, JelliPrefsJoin *join)
{
    JelliPrefsAction action = JELLI_PREFS_DO_NOTHING;
    for (const char *p = text; *p; ++p) {
        bool typed = false;
        for (unsigned page = 0u; page < PREFS_PAGE_COUNT && !typed; ++page) {
            PrefsKey keys[PREFS_MAX_KEYS];
            size_t count = prefs_keys(page, keys, PREFS_MAX_KEYS);
            for (size_t i = 0u; i < count && !typed; ++i)
                if (keys[i].ch == *p) {
                    prefs->page = (uint8_t)page;
                    action = jelli_prefs_tap(prefs, keys[i].box.x + 5, keys[i].box.y + 5, 0u, join);
                    typed = true;
                }
        }
        CHECK(typed);
    }
    return action;
}

static JelliPrefsAction press(JelliPrefs *prefs, PrefsKeyKind kind, JelliPrefsJoin *join)
{
    PrefsKey keys[PREFS_MAX_KEYS];
    size_t count = prefs_keys(prefs->page, keys, PREFS_MAX_KEYS);
    for (size_t i = 0u; i < count; ++i)
        if (keys[i].kind == kind)
            return jelli_prefs_tap(prefs, keys[i].box.x + 5, keys[i].box.y + 5, 0u, join);
    CHECK(false);
    return JELLI_PREFS_DO_NOTHING;
}

static bool wiped(const JelliPrefs *prefs)
{
    for (size_t i = 0u; i < sizeof(prefs->password); ++i)
        if (prefs->password[i])
            return false;
    return prefs->password_size == 0u;
}

static void scan_into_list(JelliPrefs *prefs, uint64_t now)
{
    JelliPrefsNetwork found[] = {
        network("Cafe", -80, JELLI_PREFS_OPEN_NETWORK),
        network("Home", -70, JELLI_PREFS_PASSWORD_NETWORK),
        network("", -20, JELLI_PREFS_OPEN_NETWORK), /* Hidden: never listed. */
        network("Home", -50, JELLI_PREFS_PASSWORD_NETWORK),
        network("Office", -60, JELLI_PREFS_ENTERPRISE_NETWORK),
    };
    tap_part(prefs, PREFS_MENU_WIFI, JELLI_PREFS_DO_SCAN, now, NULL);
    CHECK(prefs->screen == JELLI_PREFS_SCANNING);
    jelli_prefs_scanned(prefs, found, sizeof(found) / sizeof(found[0]), true, now);
    CHECK(prefs->screen == JELLI_PREFS_LIST && prefs->count == 3u && !prefs->scan_failed);
    CHECK(!memcmp(prefs->networks[0].ssid, "Home", 4u) && prefs->networks[0].rssi == -50);
    CHECK(!memcmp(prefs->networks[1].ssid, "Office", 6u));
    CHECK(!memcmp(prefs->networks[2].ssid, "Cafe", 4u));
}

static void test_a_password_network_joins_with_what_was_typed(void)
{
    JelliPrefs prefs;
    JelliPrefsJoin join;
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, NULL, 0u, false, 100u);
    CHECK(prefs.screen == JELLI_PREFS_MENU && !prefs.connected && prefs.dirty);
    scan_into_list(&prefs, 200u);
    PrefsBox row = prefs_row_box(0u);
    CHECK(jelli_prefs_tap(&prefs, row.x + 10, row.y + 10, 300u, &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_PASSWORD && prefs.chosen == 0u && wiped(&prefs));
    CHECK(type(&prefs, "hun ter", &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(press(&prefs, PREFS_KEY_JOIN, &join) == JELLI_PREFS_DO_NOTHING); /* Seven: too short. */
    CHECK(type(&prefs, "!2", &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(press(&prefs, PREFS_KEY_DELETE, &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.password_size == 8u && !strcmp(prefs.password, "hun ter!"));
    memset(&join, 0x55, sizeof(join));
    CHECK(press(&prefs, PREFS_KEY_JOIN, &join) == JELLI_PREFS_DO_JOIN);
    CHECK(join.ssid_size == 4u && !memcmp(join.ssid, "Home", 4u));
    CHECK(join.password_size == 8u && !strcmp(join.password, "hun ter!"));
    CHECK(prefs.screen == JELLI_PREFS_JOINING && wiped(&prefs)); /* Handed over, gone here. */
    jelli_prefs_joined(&prefs, JELLI_PREFS_WRONG_PASSWORD, 400u);
    CHECK(prefs.screen == JELLI_PREFS_RESULT && prefs.outcome == JELLI_PREFS_WRONG_PASSWORD);
    tap_part(&prefs, PREFS_RESULT_RETRY, JELLI_PREFS_DO_NOTHING, 500u, &join);
    CHECK(prefs.screen == JELLI_PREFS_PASSWORD && wiped(&prefs) && prefs.page == PREFS_LOWER);
    CHECK(type(&prefs, "correct horse", &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(press(&prefs, PREFS_KEY_JOIN, &join) == JELLI_PREFS_DO_JOIN);
    jelli_prefs_joined(&prefs, JELLI_PREFS_JOINED, 600u);
    CHECK(prefs.screen == JELLI_PREFS_RESULT && prefs.connected && prefs.current_size == 4u);
    CHECK(!jelli_prefs_tick(&prefs, 600u + JELLI_PREFS_RESULT_MS - 1u) || prefs.dirty);
    prefs.dirty = false;
    CHECK(jelli_prefs_tick(&prefs, 600u + JELLI_PREFS_RESULT_MS));
    CHECK(prefs.screen == JELLI_PREFS_MENU); /* Back to Settings, showing the new network. */
    tap_part(&prefs, PREFS_MENU_DONE, JELLI_PREFS_DO_CLOSE, 3000u, NULL);
    CHECK(prefs.screen == JELLI_PREFS_CLOSED && prefs.count == 0u && wiped(&prefs));
}

static void test_open_and_enterprise_networks(void)
{
    JelliPrefs prefs;
    JelliPrefsJoin join;
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, (const uint8_t *)"Home", 4u, true, 0u);
    CHECK(prefs.connected && prefs.current_size == 4u);
    scan_into_list(&prefs, 0u);
    PrefsBox office = prefs_row_box(1u), cafe = prefs_row_box(2u);
    CHECK(jelli_prefs_tap(&prefs, office.x + 9, office.y + 9, 0u, &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_RESULT && prefs.outcome == JELLI_PREFS_UNSUPPORTED);
    tap_part(&prefs, PREFS_RESULT_RETRY, JELLI_PREFS_DO_NOTHING, 0u, &join); /* Not offered. */
    CHECK(prefs.screen == JELLI_PREFS_RESULT);
    tap_part(&prefs, PREFS_RESULT_ALONE, JELLI_PREFS_DO_NOTHING, 0u, &join);
    CHECK(prefs.screen == JELLI_PREFS_LIST);
    CHECK(jelli_prefs_tap(&prefs, cafe.x + 9, cafe.y + 9, 0u, &join) == JELLI_PREFS_DO_JOIN);
    CHECK(join.ssid_size == 4u && join.password_size == 0u && join.password[0] == '\0');
    jelli_prefs_joined(&prefs, JELLI_PREFS_NOT_FOUND, 10u);
    tap_part(&prefs, PREFS_RESULT_RETRY, JELLI_PREFS_DO_JOIN, 20u,
             &join); /* Open: again at once. */
    jelli_prefs_joined(&prefs, JELLI_PREFS_FAILED, 30u);
    tap_part(&prefs, PREFS_RESULT_BACK, JELLI_PREFS_DO_NOTHING, 40u, &join);
    CHECK(prefs.screen == JELLI_PREFS_LIST);
    /* Without a request to fill, nothing is joined and the screen stays. */
    CHECK(jelli_prefs_tap(&prefs, cafe.x + 9, cafe.y + 9, 0u, NULL) == JELLI_PREFS_DO_NOTHING);
}

static void test_results_only_count_when_awaited(void)
{
    JelliPrefs prefs;
    jelli_prefs_init(&prefs);
    JelliPrefsNetwork one = network("One", -40, JELLI_PREFS_OPEN_NETWORK);
    jelli_prefs_scanned(&prefs, &one, 1u, true, 0u);
    CHECK(prefs.screen == JELLI_PREFS_CLOSED && prefs.count == 0u);
    jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
    jelli_prefs_scanned(&prefs, &one, 1u, true, 0u);
    jelli_prefs_joined(&prefs, JELLI_PREFS_JOINED, 0u);
    CHECK(prefs.screen == JELLI_PREFS_MENU && prefs.count == 0u && !prefs.connected);
    tap_part(&prefs, PREFS_MENU_WIFI, JELLI_PREFS_DO_SCAN, 0u, NULL);
    tap_part(&prefs, PREFS_SCAN_BACK, JELLI_PREFS_DO_NOTHING, 0u, NULL);
    jelli_prefs_scanned(&prefs, &one, 1u, true, 0u); /* Too late: the user went back. */
    CHECK(prefs.screen == JELLI_PREFS_MENU && prefs.count == 0u);
    tap_part(&prefs, PREFS_MENU_WIFI, JELLI_PREFS_DO_SCAN, 0u, NULL);
    jelli_prefs_scanned(&prefs, &one, 1u, false, 0u);
    CHECK(prefs.screen == JELLI_PREFS_LIST && prefs.scan_failed && prefs.count == 0u);
    tap_part(&prefs, PREFS_LIST_SCAN, JELLI_PREFS_DO_SCAN, 0u, NULL);
    jelli_prefs_scanned(&prefs, NULL, 3u, true, 0u);
    CHECK(prefs.screen == JELLI_PREFS_LIST && !prefs.scan_failed && prefs.count == 0u);
}

/* Twenty names, the strongest sixteen kept in order, scrolled a row at a time. */
static void test_many_networks_scroll(void)
{
    JelliPrefs prefs;
    JelliPrefsNetwork found[20];
    char name[8];
    for (int i = 0; i < 20; ++i) {
        (void)snprintf(name, sizeof(name), "net%02d", i);
        found[i] = network(name, -90 + i, JELLI_PREFS_PASSWORD_NETWORK);
    }
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
    tap_part(&prefs, PREFS_MENU_WIFI, JELLI_PREFS_DO_SCAN, 0u, NULL);
    jelli_prefs_scanned(&prefs, found, 20u, true, 0u);
    CHECK(prefs.count == JELLI_PREFS_MAX_NETWORKS);
    CHECK(!memcmp(prefs.networks[0].ssid, "net19", 5u) &&
          !memcmp(prefs.networks[15].ssid, "net04", 5u));
    jelli_prefs_drag(&prefs, -(PREFS_ROW_PITCH - 1));
    CHECK(prefs.top == 0u);
    jelli_prefs_drag(&prefs, -1);
    CHECK(prefs.top == 1u);
    jelli_prefs_drag(&prefs, -PREFS_ROW_PITCH * 40);
    CHECK(prefs.top == JELLI_PREFS_MAX_NETWORKS - PREFS_LIST_ROWS && prefs.drag == 0);
    jelli_prefs_drag(&prefs, PREFS_ROW_PITCH * 3);
    CHECK(prefs.top == JELLI_PREFS_MAX_NETWORKS - PREFS_LIST_ROWS - 3u);
    jelli_prefs_drag(&prefs, 1000000); /* Out of range: ignored. */
    CHECK(prefs.top == JELLI_PREFS_MAX_NETWORKS - PREFS_LIST_ROWS - 3u);
    PrefsBox row = prefs_row_box(2u);
    JelliPrefsJoin join;
    CHECK(jelli_prefs_tap(&prefs, row.x + 1, row.y + 1, 0u, &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_PASSWORD && prefs.chosen == prefs.top + 2u);
}

static void test_keyboard_pages_and_limits(void)
{
    JelliPrefs prefs;
    JelliPrefsJoin join;
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
    scan_into_list(&prefs, 0u);
    PrefsBox row = prefs_row_box(0u);
    (void)jelli_prefs_tap(&prefs, row.x + 1, row.y + 1, 0u, &join);
    CHECK(press(&prefs, PREFS_KEY_SHIFT, &join) == JELLI_PREFS_DO_NOTHING &&
          prefs.page == PREFS_UPPER);
    CHECK(press(&prefs, PREFS_KEY_SHIFT, &join) == JELLI_PREFS_DO_NOTHING &&
          prefs.page == PREFS_LOWER);
    (void)press(&prefs, PREFS_KEY_DIGITS, &join);
    CHECK(prefs.page == PREFS_DIGITS);
    (void)press(&prefs, PREFS_KEY_MORE, &join);
    CHECK(prefs.page == PREFS_SYMBOLS);
    (void)press(&prefs, PREFS_KEY_MORE, &join);
    CHECK(prefs.page == PREFS_DIGITS);
    (void)press(&prefs, PREFS_KEY_LETTERS, &join);
    CHECK(prefs.page == PREFS_LOWER);
    for (int i = 0; i < 70; ++i)
        (void)press(&prefs, PREFS_KEY_SPACE, &join);
    CHECK(prefs.password_size == JELLI_PREFS_PASSWORD_MAX && prefs.password[63] == '\0');
    for (int i = 0; i < 70; ++i)
        (void)press(&prefs, PREFS_KEY_DELETE, &join);
    CHECK(wiped(&prefs));
    tap_part(&prefs, PREFS_PASS_SHOW, JELLI_PREFS_DO_NOTHING, 0u, &join);
    CHECK(prefs.reveal);
    (void)type(&prefs, "abc", &join);
    tap_part(&prefs, PREFS_PASS_CANCEL, JELLI_PREFS_DO_NOTHING, 0u, &join);
    CHECK(prefs.screen == JELLI_PREFS_LIST && wiped(&prefs) && !prefs.reveal);
    (void)jelli_prefs_tap(&prefs, row.x + 1, row.y + 1, 0u, &join);
    (void)type(&prefs, "secret12", &join);
    jelli_prefs_close(&prefs); /* The side button, mid-typing. */
    CHECK(prefs.screen == JELLI_PREFS_CLOSED && wiped(&prefs) && prefs.current_size == 0u);
}

static void test_waiting_screens_animate(void)
{
    JelliPrefs prefs;
    jelli_prefs_init(&prefs);
    CHECK(!jelli_prefs_tick(&prefs, 0u));
    jelli_prefs_open(&prefs, NULL, 0u, false, 1000u);
    prefs.dirty = false;
    CHECK(!jelli_prefs_tick(&prefs, 900u)); /* A clock running backwards changes nothing. */
    tap_part(&prefs, PREFS_MENU_WIFI, JELLI_PREFS_DO_SCAN, 1000u, NULL);
    prefs.dirty = false;
    CHECK(!jelli_prefs_tick(&prefs, 1000u + JELLI_PREFS_ANIMATE_MS - 1u));
    CHECK(jelli_prefs_tick(&prefs, 1000u + JELLI_PREFS_ANIMATE_MS) && prefs.frame == 1u);
    prefs.dirty = false;
    CHECK(jelli_prefs_tick(&prefs, 1000u + 4u * JELLI_PREFS_ANIMATE_MS) && prefs.frame == 0u);
}

int main(void)
{
    test_layout_fits_the_round_screen();
    test_every_character_can_be_typed();
    test_a_password_network_joins_with_what_was_typed();
    test_open_and_enterprise_networks();
    test_results_only_count_when_awaited();
    test_many_networks_scroll();
    test_keyboard_pages_and_limits();
    test_waiting_screens_animate();
    printf("Settings checks passed; state=%zu bytes\n", sizeof(JelliPrefs));
    return 0;
}

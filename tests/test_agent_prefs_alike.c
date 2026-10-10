#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Settings and look-alike networks (ADR-016): anyone nearby can broadcast an open
 * network named like the board's own. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

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

/* A look-alike: an open network nearby named like the board's own, which has a
 * password. Both are listed, apart; joining any open network then takes a warning
 * first, whatever its name, since a look-alike's can differ in what the screen cannot
 * show (a trailing space, bytes drawn as '?'). */
static void test_an_open_look_alike_is_listed_apart_and_warned_about(void)
{
    JelliPrefs prefs;
    JelliPrefsJoin join;
    JelliPrefsNetwork found[] = {network("Home", -60, JELLI_PREFS_PASSWORD_NETWORK),
                                 network("Home", -35, JELLI_PREFS_OPEN_NETWORK),
                                 network("Cafe", -70, JELLI_PREFS_OPEN_NETWORK)};
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, (const uint8_t *)"Home", 4u, true, 0u);
    jelli_prefs_secured(&prefs, true);
    CHECK(prefs.current_secured);
    tap_part(&prefs, PREFS_MENU_WIFI, JELLI_PREFS_DO_SCAN, 0u, NULL);
    jelli_prefs_scanned(&prefs, found, 3u, true, 0u);
    CHECK(prefs.count == 3u && prefs.networks[0].security == JELLI_PREFS_OPEN_NETWORK);
    CHECK(prefs.networks[1].security == JELLI_PREFS_PASSWORD_NETWORK);
    PrefsBox copy = prefs_row_box(0u), cafe = prefs_row_box(2u);
    memset(&join, 0x55, sizeof(join));
    CHECK(jelli_prefs_tap(&prefs, copy.x + 9, copy.y + 9, 0u, &join) == JELLI_PREFS_DO_NOTHING);
    CHECK(prefs.screen == JELLI_PREFS_RESULT && prefs.outcome == JELLI_PREFS_NOT_LOCKED);
    CHECK(join.ssid[0] == 0x55u); /* Nothing asked of the host yet. */
    tap_part(&prefs, PREFS_RESULT_BACK, JELLI_PREFS_DO_NOTHING, 0u, &join);
    CHECK(prefs.screen == JELLI_PREFS_LIST);
    (void)jelli_prefs_tap(&prefs, copy.x + 9, copy.y + 9, 0u, &join);
    tap_part(&prefs, PREFS_RESULT_RETRY, JELLI_PREFS_DO_JOIN, 0u, &join); /* Knowingly. */
    CHECK(join.ssid_size == 4u && join.password_size == 0u);
    jelli_prefs_joined(&prefs, JELLI_PREFS_JOINED, 0u);
    CHECK(prefs.current_size == 4u && !prefs.current_secured); /* Now the open one is it. */
    /* An open network that copies nothing joins at once. */
    tap_part(&prefs, PREFS_RESULT_ALONE, JELLI_PREFS_DO_NOTHING, 0u, &join);
    tap_part(&prefs, PREFS_MENU_WIFI, JELLI_PREFS_DO_SCAN, 0u, NULL);
    jelli_prefs_scanned(&prefs, found, 3u, true, 0u);
    CHECK(jelli_prefs_tap(&prefs, cafe.x + 9, cafe.y + 9, 0u, &join) == JELLI_PREFS_DO_JOIN);
    /* The host reports how a join went, never these two. */
    jelli_prefs_joined(&prefs, JELLI_PREFS_UNSUPPORTED, 0u);
    jelli_prefs_joined(&prefs, JELLI_PREFS_NOT_LOCKED, 0u);
    CHECK(prefs.screen == JELLI_PREFS_JOINING);
    /* Every open network warned while the board's own was locked, whatever its name. */
    JelliPrefsNetwork alike[] = {network("Home ", -30, JELLI_PREFS_OPEN_NETWORK),
                                 network("Hom\xff", -31, JELLI_PREFS_OPEN_NETWORK),
                                 network("Home", -60, JELLI_PREFS_PASSWORD_NETWORK)};
    jelli_prefs_open(&prefs, (const uint8_t *)"Home", 4u, true, 0u);
    jelli_prefs_secured(&prefs, true);
    for (unsigned row = 0u; row < 2u; ++row) {
        tap_part(&prefs, PREFS_MENU_WIFI, JELLI_PREFS_DO_SCAN, 0u, NULL);
        jelli_prefs_scanned(&prefs, alike, 3u, true, 0u);
        PrefsBox box = prefs_row_box(row);
        CHECK(jelli_prefs_tap(&prefs, box.x + 9, box.y + 9, 0u, &join) == JELLI_PREFS_DO_NOTHING);
        CHECK(prefs.screen == JELLI_PREFS_RESULT && prefs.outcome == JELLI_PREFS_NOT_LOCKED);
        tap_part(&prefs, PREFS_RESULT_BACK, JELLI_PREFS_DO_NOTHING, 0u, &join);
        tap_part(&prefs, PREFS_LIST_BACK, JELLI_PREFS_DO_NOTHING, 0u, &join);
    }
    jelli_prefs_secured(&prefs, false);
    jelli_prefs_secured(&prefs, true);
    jelli_prefs_close(&prefs);
    CHECK(!prefs.current_secured);
    jelli_prefs_secured(&prefs, true); /* No network: nothing to be locked. */
    CHECK(!prefs.current_secured);
    jelli_prefs_secured(NULL, true);
}

int main(void)
{
    test_an_open_look_alike_is_listed_apart_and_warned_about();
    printf("Look-alike network checks passed\n");
    return 0;
}

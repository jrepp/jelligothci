#include "../core/agent/prefs_draw.h" /* With agent_draw.h and agent_prefs.h. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Settings drawn from any state its API can reach (ADR-016): seeded runs of taps aimed
 * at what each screen shows, drags, scans of any bytes, join outcomes, ticks, and
 * closes, drawing after each, never write outside the surface or its round panel and
 * draw the same pixels twice. A hidden password shows only its length, a shown one only
 * its latest thirteen characters; rows show bars, names, and arrows as they should; and
 * a small surface clips it all. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: seed %u step %u: %s\n", __FILE__, __LINE__, seed, step,        \
                    #expr);                                                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SIDE = 466, STRIDE = 480, ROWS = 470, GUARD = 0x5a5a };
static uint16_t pixels[STRIDE * ROWS], again[STRIDE * ROWS];
static unsigned seed, step, seen[JELLI_PREFS_RESULT + 1];
static uint32_t state;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static JelliSurface cleared(uint16_t *memory, unsigned width, unsigned height, unsigned stride)
{
    for (size_t i = 0u; i < (size_t)STRIDE * ROWS; ++i)
        memory[i] = GUARD;
    return (JelliSurface){memory, width, height, stride, {0u, 0u, 0u, 0u}};
}

static bool same_networks(const JelliPrefs *a, const JelliPrefs *b)
{
    for (unsigned i = 0u; i < JELLI_PREFS_MAX_NETWORKS; ++i)
        if (a->networks[i].ssid_size != b->networks[i].ssid_size ||
            a->networks[i].rssi != b->networks[i].rssi ||
            a->networks[i].security != b->networks[i].security ||
            memcmp(a->networks[i].ssid, b->networks[i].ssid, JELLI_PREFS_SSID_MAX) != 0)
            return false;
    return true;
}

/* Everything but dirty, field by field (the struct has padding). */
static bool same_state(const JelliPrefs *a, const JelliPrefs *b)
{
    return same_networks(a, b) && a->screen == b->screen &&
           !memcmp(a->current, b->current, sizeof(a->current)) &&
           a->current_size == b->current_size && a->connected == b->connected &&
           a->scan_failed == b->scan_failed && a->count == b->count && a->top == b->top &&
           a->chosen == b->chosen && !memcmp(a->password, b->password, sizeof(a->password)) &&
           a->password_size == b->password_size && a->page == b->page && a->reveal == b->reveal &&
           a->outcome == b->outcome && a->drag == b->drag && a->since_ms == b->since_ms &&
           a->frame == b->frame;
}

/* Draws over guard values: nothing outside the surface or the panel's circle. */
static void draw_checked(JelliPrefs *prefs, uint16_t *memory)
{
    JelliSurface surface = cleared(memory, SIDE, SIDE, STRIDE);
    JelliPrefs before = *prefs;
    jelli_prefs_draw(prefs, &surface);
    CHECK(!prefs->dirty && same_state(&before, prefs)); /* Drawing changes nothing else. */
    CHECK(surface.damage.x == 0u && surface.damage.width == SIDE && surface.damage.height == SIDE);
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < STRIDE; ++x) {
            uint16_t pixel = memory[y * STRIDE + x];
            int64_t dx = x - 233, dy = y - 233;
            if (x >= SIDE || y >= SIDE) {
                CHECK(pixel == GUARD);
            } else if (pixel != 0u) {
                CHECK(dx * dx + dy * dy <= (int64_t)233 * 233);
                CHECK(prefs->screen != JELLI_PREFS_CLOSED); /* Closed: all black. */
            }
        }
}

/* The boxes the current screen responds to; on the keyboard, its keys too. */
static size_t targets(const JelliPrefs *prefs, PrefsBox *boxes)
{
    static const PrefsPart parts[][3] = {
        [JELLI_PREFS_MENU] = {PREFS_MENU_WIFI, PREFS_MENU_DONE, PREFS_MENU_WIFI},
        [JELLI_PREFS_SCANNING] = {PREFS_SCAN_BACK, PREFS_SCAN_BACK, PREFS_SCAN_BACK},
        [JELLI_PREFS_LIST] = {PREFS_LIST_BACK, PREFS_LIST_SCAN, PREFS_LIST_SCAN},
        [JELLI_PREFS_PASSWORD] = {PREFS_PASS_SHOW, PREFS_PASS_CANCEL, PREFS_PASS_FIELD},
        [JELLI_PREFS_RESULT] = {PREFS_RESULT_RETRY, PREFS_RESULT_BACK, PREFS_RESULT_ALONE},
    };
    size_t count = 0u;
    if (prefs->screen == JELLI_PREFS_CLOSED || prefs->screen == JELLI_PREFS_JOINING)
        return 0u;
    for (unsigned i = 0u; i < 3u; ++i)
        boxes[count++] = prefs_box(parts[prefs->screen][i]);
    for (unsigned row = 0u; prefs->screen == JELLI_PREFS_LIST && row < PREFS_LIST_ROWS; ++row)
        for (unsigned weight = 0u; weight < 3u; ++weight) /* Choosing a row is the way on. */
            boxes[count++] = prefs_row_box(row);
    if (prefs->screen == JELLI_PREFS_PASSWORD) {
        PrefsKey keys[PREFS_MAX_KEYS];
        size_t keys_count = prefs_keys(prefs->page, keys, PREFS_MAX_KEYS);
        for (size_t i = 0u; i < keys_count; ++i) /* Page keys weigh more than letters. */
            for (unsigned weight = keys[i].kind == PREFS_KEY_CHAR ? 2u : 0u; weight < 3u; ++weight)
                boxes[count++] = keys[i].box;
        for (unsigned weight = 0u; weight < 4u; ++weight)
            boxes[count++] = prefs_box(PREFS_PASS_SHOW);
    }
    return count;
}

static void aimed_tap(JelliPrefs *prefs, uint64_t now, JelliPrefsJoin *join)
{
    PrefsBox boxes[3u + 3u * PREFS_LIST_ROWS + 3u * PREFS_MAX_KEYS + 4u];
    size_t count = targets(prefs, boxes);
    int x = (int)(next() % 466u), y = (int)(next() % 466u);
    if (count && next() % 5u) {
        PrefsBox box = boxes[next() % count];
        x = box.x + (int)(next() % (unsigned)box.width);
        y = box.y + (int)(next() % (unsigned)box.height);
    }
    (void)jelli_prefs_tap(prefs, x, y, now, join);
}

/* Up to 24 networks with names of any bytes and lengths, some the widest allowed. */
static void any_scan(JelliPrefs *prefs, uint64_t now)
{
    JelliPrefsNetwork found[24];
    size_t count = next() % 25u;
    for (size_t i = 0u; i < count; ++i) {
        uint8_t size = (uint8_t)((next() & 1u) ? JELLI_PREFS_SSID_MAX : next() % 34u);
        found[i] = (JelliPrefsNetwork){{0}, size, (int8_t)next(), (uint8_t)(next() % 4u)};
        for (unsigned b = 0u; b < JELLI_PREFS_SSID_MAX; ++b)
            found[i].ssid[b] = (uint8_t)((next() & 1u) ? 'W' : next());
    }
    jelli_prefs_scanned(prefs, found, count, next() % 6u != 0u, now);
}

/* Taps a character key again and again, as a person types a long passphrase. */
static void burst(JelliPrefs *prefs, uint64_t now, JelliPrefsJoin *join)
{
    PrefsKey keys[PREFS_MAX_KEYS];
    size_t count = prefs_keys(prefs->page, keys, PREFS_MAX_KEYS);
    for (unsigned typed = next() % 70u; typed && prefs->screen == JELLI_PREFS_PASSWORD; --typed) {
        const PrefsKey *key = &keys[next() % count];
        if (key->kind == PREFS_KEY_CHAR || key->kind == PREFS_KEY_SPACE)
            (void)jelli_prefs_tap(prefs, key->box.x + 3, key->box.y + 3, now, join);
    }
}

/* Mostly what the screen awaits: results while waiting, typing on the keyboard, taps. */
static void one_step(JelliPrefs *prefs, uint64_t *now, JelliPrefsJoin *join)
{
    static const uint8_t wide[] = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"; /* 33: one too many. */
    unsigned roll = next() % 40u;
    bool waiting = prefs->screen == JELLI_PREFS_SCANNING || prefs->screen == JELLI_PREFS_JOINING;
    *now += next() % 900u;
    if (roll == 0u)
        jelli_prefs_open(prefs, wide, next() % 34u, next() & 1u, *now);
    else if (roll == 1u)
        jelli_prefs_close(prefs);
    else if (roll < 5u)
        (void)jelli_prefs_tick(prefs, *now);
    else if (roll < 10u)
        jelli_prefs_drag(prefs, (int)(next() % 400u) - 200);
    else if (roll < (waiting ? 30u : 12u) && (next() & 1u))
        any_scan(prefs, *now);
    else if (roll < (waiting ? 30u : 12u))
        jelli_prefs_joined(prefs, (JelliPrefsOutcome)(next() % 6u % 5u), *now); /* Joined most. */
    else if (roll < 18u && prefs->screen == JELLI_PREFS_PASSWORD)
        burst(prefs, *now, join);
    else
        aimed_tap(prefs, *now, join);
}

static void test_every_reachable_state_draws_inside_the_panel(void)
{
    for (seed = 0u; seed < 10u; ++seed) {
        JelliPrefs prefs;
        JelliPrefsJoin join;
        uint64_t now = 0u;
        state = 0x7f4a7c15u ^ (seed * 2654435761u);
        jelli_prefs_init(&prefs);
        jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
        for (step = 0u; step < 200u; ++step) {
            one_step(&prefs, &now, &join);
            draw_checked(&prefs, pixels);
            ++seen[prefs.screen];
            if (step % 8u == 0u) {
                draw_checked(&prefs, again);
                CHECK(
                    !memcmp(pixels, again, sizeof(pixels))); /* The same state, the same pixels. */
            }
        }
    }
    step = 0u;
    for (unsigned screen = JELLI_PREFS_MENU; screen <= JELLI_PREFS_RESULT; ++screen)
        CHECK(seen[screen] >= 10u); /* Every screen was drawn, many times. */
}

/* The password screen with a typed password of size bytes from fill. */
static void password_screen(JelliPrefs *prefs, const char *fill, size_t size, bool reveal)
{
    static const JelliPrefsNetwork home = {{'H', 'o', 'm', 'e'}, 4u, -50, 1u};
    PrefsBox row = prefs_row_box(0u);
    JelliPrefsJoin join;
    jelli_prefs_init(prefs);
    jelli_prefs_open(prefs, NULL, 0u, false, 0u);
    (void)jelli_prefs_tap(prefs, 233, 188, 0u, NULL); /* Wi-Fi. */
    jelli_prefs_scanned(prefs, &home, 1u, true, 0u);
    (void)jelli_prefs_tap(prefs, row.x + 9, row.y + 9, 0u, &join);
    CHECK(prefs->screen == JELLI_PREFS_PASSWORD);
    memcpy(prefs->password, fill, size);
    prefs->password_size = (uint8_t)size;
    prefs->reveal = reveal;
}

/* Hidden, two passwords of a size draw alike; shown, they differ, but only by their
 * latest thirteen characters. */
static void test_the_field_shows_only_what_it_should(void)
{
    static const char a[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!";
    static const char b[] = "~|{}[]<>()%$#@&*+=-_^`'\"?.,;:!/\\ zyxwvutsrqponmlkjihgfedcbaZYX";
    for (step = 0u; step <= JELLI_PREFS_PASSWORD_MAX; ++step) {
        JelliPrefs prefs;
        password_screen(&prefs, a, step, false);
        draw_checked(&prefs, pixels);
        password_screen(&prefs, b, step, false);
        draw_checked(&prefs, again);
        CHECK(!memcmp(pixels, again, sizeof(pixels))); /* Only its length shows. */
        password_screen(&prefs, a, step, true);
        draw_checked(&prefs, pixels);
        password_screen(&prefs, b, step, true);
        draw_checked(&prefs, again);
        CHECK(step == 0u || memcmp(pixels, again, sizeof(pixels)) != 0);
        if (step > 13u) { /* The same latest thirteen: the same field. */
            char mixed[JELLI_PREFS_PASSWORD_MAX];
            memcpy(mixed, b, step - 13u);
            memcpy(mixed + step - 13u, a + step - 13u, 13u);
            password_screen(&prefs, a, step, true);
            draw_checked(&prefs, pixels);
            password_screen(&prefs, mixed, step, true);
            draw_checked(&prefs, again);
            CHECK(!memcmp(pixels, again, sizeof(pixels)));
        }
    }
}

/* Every page, shown or hidden, at the sizes where the field and Join change, and every
 * outcome, all for a network with the widest name; then a full list at every scroll. */
static void test_every_screen_at_its_widest(void)
{
    static const size_t sizes[] = {0u, 1u, 7u, 8u, 13u, 14u, 62u, 63u};
    JelliPrefsNetwork wide = {{0}, JELLI_PREFS_SSID_MAX, -30, 1u};
    memset(wide.ssid, 'W', sizeof(wide.ssid));
    step = 0u;
    for (unsigned page = 0u; page < PREFS_PAGE_COUNT; ++page)
        for (size_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); ++i, ++step) {
            JelliPrefs prefs;
            password_screen(&prefs,
                            "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
                            sizes[i], (step & 1u) != 0u);
            prefs.networks[0] = wide;
            prefs.page = (uint8_t)page;
            draw_checked(&prefs, pixels);
            prefs.screen = JELLI_PREFS_RESULT;
            prefs.outcome = (uint8_t)(step % 5u); /* Each outcome, twice per page. */
            draw_checked(&prefs, pixels);
        }
    JelliPrefs prefs;
    JelliPrefsNetwork found[JELLI_PREFS_MAX_NETWORKS];
    for (unsigned i = 0u; i < JELLI_PREFS_MAX_NETWORKS; ++i) {
        found[i] = wide;
        found[i].ssid[0] = (uint8_t)(0xf0u + i); /* Distinct, unprintable names. */
        found[i].rssi = (int8_t)(-40 - 4 * (int)i);
        found[i].security = (uint8_t)(i % 3u);
    }
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, found[3].ssid, JELLI_PREFS_SSID_MAX, true, 0u); /* Outlined. */
    (void)jelli_prefs_tap(&prefs, 233, 188, 0u, NULL);
    jelli_prefs_scanned(&prefs, found, JELLI_PREFS_MAX_NETWORKS, true, 0u);
    for (step = 0u; step <= JELLI_PREFS_MAX_NETWORKS - PREFS_LIST_ROWS; ++step) {
        CHECK(prefs.top == step);
        draw_checked(&prefs, pixels);
        jelli_prefs_drag(&prefs, -PREFS_ROW_PITCH);
    }
}

/* A list of count open networks, the first named name, at rssi, scrolled to top. */
static void list_of(JelliPrefs *prefs, const char *name, int rssi, unsigned count, unsigned top)
{
    JelliPrefsNetwork found[JELLI_PREFS_MAX_NETWORKS];
    for (unsigned i = 0u; i < count; ++i) {
        found[i] = (JelliPrefsNetwork){{(uint8_t)('A' + i)}, 1u, (int8_t)(rssi - (int)i), 0u};
        if (!i) {
            found[i].ssid_size = (uint8_t)strlen(name);
            memcpy(found[i].ssid, name, found[i].ssid_size);
        }
    }
    jelli_prefs_init(prefs);
    jelli_prefs_open(prefs, NULL, 0u, false, 0u);
    (void)jelli_prefs_tap(prefs, 233, 188, 0u, NULL);
    jelli_prefs_scanned(prefs, found, count, true, 0u);
    jelli_prefs_drag(prefs, -PREFS_ROW_PITCH * (int)top);
    CHECK(prefs->top == top);
}

static bool lists_look_alike(const char *a, int rssi_a, const char *b, int rssi_b)
{
    JelliPrefs prefs;
    list_of(&prefs, a, rssi_a, 1u, 0u);
    draw_checked(&prefs, pixels);
    list_of(&prefs, b, rssi_b, 1u, 0u);
    draw_checked(&prefs, again);
    return !memcmp(pixels, again, sizeof(pixels));
}

/* Grey in the gap above the first row or below the last: a "more" arrow. */
static bool arrow(int top)
{
    for (int y = top; y < top + 8; ++y)
        for (int x = PREFS_CENTER - 8; x <= PREFS_CENTER + 8; ++x)
            if (pixels[y * STRIDE + x] == PREFS_GREY)
                return true;
    return false;
}

/* Bars change exactly at -55, -67, and -78 dBm; a name shows whole up to fifteen
 * glyphs, else its first thirteen and ".."; arrows show exactly when rows are hidden. */
static void test_rows_show_signal_names_and_more(void)
{
    static const int same[][2] = {{-40, -55}, {-56, -67}, {-68, -78}, {-79, -128}, {127, -55}};
    static const int differ[][2] = {{-55, -56}, {-67, -68}, {-78, -79}};
    for (step = 0u; step < 5u; ++step)
        CHECK(lists_look_alike("Bars", same[step][0], "Bars", same[step][1]));
    for (step = 0u; step < 3u; ++step)
        CHECK(!lists_look_alike("Bars", differ[step][0], "Bars", differ[step][1]));
    step = 0u;
    CHECK(!lists_look_alike("Neighbourhood-1", -50, "Neighbourhood-2", -50)); /* Whole. */
    CHECK(lists_look_alike("Neighbourhood-12", -50, "Neighbourhood-13", -50));
    CHECK(lists_look_alike("Neighbourhood network one", -50, "Neighbourhoodxxxxxxxxxxxxxxxxxxx",
                           -50));
    for (unsigned count = 0u; count <= JELLI_PREFS_MAX_NETWORKS; ++count) {
        unsigned last = count > PREFS_LIST_ROWS ? count - PREFS_LIST_ROWS : 0u;
        for (unsigned top = 0u; top <= last; ++top) {
            JelliPrefs prefs;
            step = count * 100u + top;
            list_of(&prefs, "Net", -40, count, top);
            draw_checked(&prefs, pixels);
            CHECK(arrow(88) == (top > 0u));
            CHECK(arrow(344) == (top + PREFS_LIST_ROWS < count));
        }
    }
}

/* On a surface smaller than the panel every screen still draws only inside it. */
static void test_a_small_surface_clips_every_screen(void)
{
    enum { W = 200, H = 150, PITCH = 210 };
    seed = 99u;
    state = 0x2545f491u;
    JelliPrefs prefs;
    JelliPrefsJoin join;
    uint64_t now = 0u;
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
    for (step = 0u; step < 300u; ++step) {
        one_step(&prefs, &now, &join);
        JelliSurface surface = cleared(pixels, W, H, PITCH);
        jelli_prefs_draw(&prefs, &surface);
        for (size_t i = 0u; i < (size_t)STRIDE * ROWS; ++i)
            CHECK(i < (size_t)PITCH * H ? (i % PITCH < W || pixels[i] == GUARD)
                                        : pixels[i] == GUARD);
        CHECK(surface.damage.width == W && surface.damage.height == H);
    }
    JelliSurface empty = {NULL, SIDE, SIDE, STRIDE, {0u, 0u, 0u, 0u}};
    prefs.dirty = true;
    jelli_prefs_draw(&prefs, &empty); /* Nowhere to draw: still to be drawn. */
    CHECK(prefs.dirty && empty.damage.width == 0u);
    jelli_prefs_draw(NULL, &empty);
    jelli_prefs_draw(&prefs, NULL);
}

int main(void)
{
    test_every_reachable_state_draws_inside_the_panel();
    test_the_field_shows_only_what_it_should();
    test_every_screen_at_its_widest();
    test_rows_show_signal_names_and_more();
    test_a_small_surface_clips_every_screen();
    printf("Settings screens drawn from fuzzed states stay inside the panel\n");
    return 0;
}

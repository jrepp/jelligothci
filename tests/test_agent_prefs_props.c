#include "../core/agent/prefs_internal.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Settings' keyboard and list, white-box (ADR-016): key lists truncate cleanly, each
 * page has its letters and one of each control, the key under every pixel is the key
 * whose box holds it, every key does exactly what a model of typing says, hit boxes
 * stop at their edges, and scrolling keeps a row's worth of drag at most and the list
 * in range, conserving drag until an end stops it. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: case %u: %s\n", __FILE__, __LINE__, which, #expr);             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned which;
static uint32_t state;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static bool same_key(const PrefsKey *a, const PrefsKey *b)
{
    return a->kind == b->kind && a->ch == b->ch && a->box.x == b->box.x && a->box.y == b->box.y &&
           a->box.width == b->box.width && a->box.height == b->box.height;
}

/* A short buffer gets the list's first keys and nothing past its end. */
static void test_keys_truncate_to_the_capacity(void)
{
    for (unsigned page = 0u; page < PREFS_PAGE_COUNT; ++page) {
        PrefsKey full[PREFS_MAX_KEYS];
        size_t count = prefs_keys(page, full, PREFS_MAX_KEYS);
        for (size_t capacity = 0u; capacity <= PREFS_MAX_KEYS; ++capacity) {
            which = page * 100u + (unsigned)capacity;
            PrefsKey keys[PREFS_MAX_KEYS + 1u];
            memset(keys, 0xee, sizeof(keys));
            size_t got = prefs_keys(page, keys, capacity);
            CHECK(got == (capacity < count ? capacity : count));
            for (size_t i = 0u; i < got; ++i)
                CHECK(same_key(&keys[i], &full[i]));
            const uint8_t *rest = (const uint8_t *)&keys[got];
            for (size_t b = 0u; b < sizeof(keys) - got * sizeof(keys[0]); ++b)
                CHECK(rest[b] == 0xeeu);
        }
    }
    which = 0u;
    CHECK(prefs_keys(0u, NULL, 10u) == 0u && prefs_keys(99u, NULL, 0u) == 0u);
    CHECK(!prefs_key_at(PREFS_PAGE_COUNT, 233, 233, NULL));
}

static unsigned count_kind(const PrefsKey *keys, size_t count, PrefsKeyKind kind)
{
    unsigned found = 0u;
    for (size_t i = 0u; i < count; ++i)
        if (keys[i].kind == kind)
            ++found;
    return found;
}

/* Every page has one of each control: shift and the digits key on letters, the
 * symbols and letters keys elsewhere, and delete, space, and Join everywhere. */
static void check_controls(unsigned page, const PrefsKey *keys, size_t count)
{
    bool letters = page == PREFS_LOWER || page == PREFS_UPPER;
    CHECK(count_kind(keys, count, PREFS_KEY_SHIFT) == (letters ? 1u : 0u));
    CHECK(count_kind(keys, count, PREFS_KEY_DIGITS) == (letters ? 1u : 0u));
    CHECK(count_kind(keys, count, PREFS_KEY_MORE) == (letters ? 0u : 1u));
    CHECK(count_kind(keys, count, PREFS_KEY_LETTERS) == (letters ? 0u : 1u));
    CHECK(count_kind(keys, count, PREFS_KEY_DELETE) == 1u);
    CHECK(count_kind(keys, count, PREFS_KEY_SPACE) == 1u);
    CHECK(count_kind(keys, count, PREFS_KEY_JOIN) == 1u);
}

/* A character key types something visible, its page's case, and only once per page;
 * the other keys type nothing but space. */
static bool check_character(unsigned page, const PrefsKey *keys, size_t i)
{
    const PrefsKey *key = &keys[i];
    if (key->kind != PREFS_KEY_CHAR) {
        CHECK(key->ch == (key->kind == PREFS_KEY_SPACE ? ' ' : '\0'));
        return false;
    }
    CHECK(key->ch > ' ' && key->ch <= '~');
    CHECK(page != PREFS_LOWER || (key->ch >= 'a' && key->ch <= 'z'));
    CHECK(page != PREFS_UPPER || (key->ch >= 'A' && key->ch <= 'Z'));
    for (size_t j = 0u; j < i; ++j)
        CHECK(keys[j].kind != PREFS_KEY_CHAR || keys[j].ch != key->ch);
    return true;
}

/* Letters pages hold exactly their 26 letters, and keys come in reading order: four
 * rows top to bottom, each left to right. */
static void test_each_page_has_its_keys_in_order(void)
{
    for (unsigned page = 0u; page < PREFS_PAGE_COUNT; ++page) {
        which = page;
        PrefsKey keys[PREFS_MAX_KEYS];
        size_t count = prefs_keys(page, keys, PREFS_MAX_KEYS);
        check_controls(page, keys, count);
        unsigned chars = 0u, rows = 1u;
        for (size_t i = 0u; i < count; ++i) {
            if (check_character(page, keys, i))
                ++chars;
            if (!i)
                continue;
            PrefsBox box = keys[i].box, before = keys[i - 1u].box;
            CHECK(box.y > before.y || (box.y == before.y && box.x > before.x));
            if (box.y != before.y)
                ++rows;
        }
        CHECK(rows == 4u && (page > PREFS_UPPER || chars == 26u));
    }
}

/* The key under every pixel, on and around the screen, is the one whose box holds it. */
static void test_the_key_under_every_pixel(void)
{
    for (unsigned page = 0u; page < PREFS_PAGE_COUNT; ++page) {
        PrefsKey keys[PREFS_MAX_KEYS];
        size_t count = prefs_keys(page, keys, PREFS_MAX_KEYS);
        for (int y = -2; y < 468; ++y)
            for (int x = -2; x < 468; ++x) {
                which = page * 1000000u + (unsigned)((y + 2) * 470 + x + 2);
                const PrefsKey *want = NULL;
                for (size_t i = 0u; i < count && !want; ++i)
                    if (prefs_inside(keys[i].box, x, y))
                        want = &keys[i];
                PrefsKey got;
                CHECK(prefs_key_at(page, x, y, &got) == (want != NULL));
                CHECK(!want || same_key(&got, want));
                CHECK(prefs_key_at(page, x, y, NULL) == (want != NULL));
            }
    }
}

/* Each key from each page, at the sizes where typing changes, does what a model says. */
static void check_typing(unsigned page, const PrefsKey *key, size_t size)
{
    static const uint8_t pages[][4] = {
        /* After SHIFT, MORE, DIGITS, LETTERS from LOWER, UPPER, DIGITS, SYMBOLS. */
        {PREFS_UPPER, PREFS_DIGITS, PREFS_DIGITS, PREFS_LOWER},
        {PREFS_LOWER, PREFS_DIGITS, PREFS_DIGITS, PREFS_LOWER},
        {PREFS_LOWER, PREFS_SYMBOLS, PREFS_DIGITS, PREFS_LOWER},
        {PREFS_LOWER, PREFS_DIGITS, PREFS_DIGITS, PREFS_LOWER},
    };
    JelliPrefs prefs;
    jelli_prefs_init(&prefs);
    memset(prefs.password, 'p', size);
    prefs.password_size = (uint8_t)size;
    prefs.page = (uint8_t)page;
    char want[JELLI_PREFS_PASSWORD_MAX + 1] = {0};
    memset(want, 'p', size);
    size_t want_size = size;
    bool printable = key->ch >= ' ' && key->ch <= '~';
    if ((key->kind == PREFS_KEY_CHAR || key->kind == PREFS_KEY_SPACE) && printable &&
        size < JELLI_PREFS_PASSWORD_MAX)
        want[want_size++] = key->ch;
    if (key->kind == PREFS_KEY_DELETE && size)
        want[--want_size] = '\0';
    bool joins = prefs_type(&prefs, key);
    CHECK(prefs.dirty &&
          joins == (key->kind == PREFS_KEY_JOIN && size >= JELLI_PREFS_PASSWORD_MIN));
    CHECK(prefs.password_size == want_size && !memcmp(prefs.password, want, sizeof(want)));
    unsigned control = key->kind == PREFS_KEY_SHIFT     ? 0u
                       : key->kind == PREFS_KEY_MORE    ? 1u
                       : key->kind == PREFS_KEY_DIGITS  ? 2u
                       : key->kind == PREFS_KEY_LETTERS ? 3u
                                                        : 4u;
    CHECK(prefs.page == (control < 4u ? pages[page][control] : page));
}

static void test_every_key_types_as_the_model_says(void)
{
    static const size_t sizes[] = {0u, 1u, 7u, 8u, 62u, 63u};
    static const char odd[] = {'\x7f', '\x1f', '\0', (char)0x80, (char)0xff};
    for (unsigned page = 0u; page < PREFS_PAGE_COUNT; ++page) {
        PrefsKey keys[PREFS_MAX_KEYS];
        size_t count = prefs_keys(page, keys, PREFS_MAX_KEYS);
        for (size_t i = 0u; i < count; ++i)
            for (size_t s = 0u; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
                which = page * 1000u + (unsigned)(i * 10u + s);
                check_typing(page, &keys[i], sizes[s]);
            }
        for (size_t i = 0u; i < sizeof(odd); ++i) { /* Never typed, from any key. */
            PrefsKey key = {PREFS_KEY_CHAR, odd[i], {0, 0, 1, 1}};
            which = 9000u + (unsigned)i;
            check_typing(page, &key, 3u);
        }
    }
}

/* A box holds its top-left pixel but not the ones past its right or bottom edge; any
 * coordinate is safe, and an unknown part is an empty box that holds nothing. */
static void test_boxes_stop_at_their_edges(void)
{
    PrefsBox box = {10, 20, 5, 6};
    which = 0u;
    CHECK(prefs_inside(box, 10, 20) && prefs_inside(box, 14, 25));
    CHECK(!prefs_inside(box, 15, 20) && !prefs_inside(box, 10, 26));
    CHECK(!prefs_inside(box, 9, 20) && !prefs_inside(box, 10, 19));
    CHECK(!prefs_inside(box, 2147483647, 2147483647) && !prefs_inside(box, -2147483647 - 1, 20));
    PrefsBox none = prefs_box(PREFS_PART_COUNT);
    /* Out of range on purpose: an unknown part is an empty box. */
    /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
    PrefsBox wild = prefs_box((PrefsPart)999);
    CHECK(none.width == 0 && none.height == 0 && wild.width == 0 && !prefs_inside(none, 0, 0));
    for (unsigned row = 0u; row + 1u < PREFS_LIST_ROWS; ++row) {
        PrefsBox above = prefs_row_box(row), below = prefs_row_box(row + 1u);
        CHECK(above.y + above.height <= below.y && above.x == below.x);
    }
    PrefsBox wifi = prefs_box(PREFS_MENU_WIFI), done = prefs_box(PREFS_MENU_DONE);
    CHECK(wifi.y + wifi.height <= done.y);
    PrefsBox field = prefs_box(PREFS_PASS_FIELD), cancel = prefs_box(PREFS_PASS_CANCEL);
    CHECK(field.y + field.height <= cancel.y);
}

static JelliPrefs listed(unsigned count)
{
    JelliPrefs prefs;
    JelliPrefsNetwork found[JELLI_PREFS_MAX_NETWORKS];
    for (unsigned i = 0u; i < count; ++i)
        found[i] = (JelliPrefsNetwork){{(uint8_t)('A' + i)}, 1u, (int8_t)(-30 - (int)i), 1u};
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
    (void)jelli_prefs_tap(&prefs, 233, 188, 0u, NULL);
    jelli_prefs_scanned(&prefs, found, count, true, 0u);
    return prefs;
}

/* Any drags keep the list in range and less than a row of drag in hand; away from the
 * ends, drag minus a row per row scrolled is conserved, so pieces add up to the whole. */
static void test_scrolling_conserves_the_drag(void)
{
    for (which = 0u; which < 4000u; ++which) {
        state = 0xa5a5a5a5u ^ (which * 2654435761u);
        unsigned count = next() % (JELLI_PREFS_MAX_NETWORKS + 1u);
        JelliPrefs prefs = listed(count);
        unsigned last = count > PREFS_LIST_ROWS ? count - PREFS_LIST_ROWS : 0u;
        for (unsigned move = 0u; move < 12u; ++move) {
            int dy = (int)(next() % 161u) - 80;
            if (next() % 16u == 0u)
                dy = (next() & 1u) ? 4097 : -4097; /* Out of range: ignored. */
            unsigned top = prefs.top;
            int64_t held = prefs.drag;
            prefs.dirty = false;
            jelli_prefs_drag(&prefs, dy);
            if (dy == 4097 || dy == -4097) {
                CHECK(prefs.top == top && !prefs.dirty);
                continue;
            }
            CHECK(prefs.top <= last && prefs.drag > -PREFS_ROW_PITCH &&
                  prefs.drag < PREFS_ROW_PITCH);
            CHECK(prefs.dirty == (prefs.top != top));
            /* Conserved, or stopped at an end with the rest of the drag let go. */
            int64_t after = prefs.drag - (int64_t)PREFS_ROW_PITCH * prefs.top;
            CHECK(after == held + dy - (int64_t)PREFS_ROW_PITCH * top ||
                  (prefs.drag == 0 && (prefs.top == 0u || prefs.top == last)));
        }
    }
    JelliPrefs prefs = listed(JELLI_PREFS_MAX_NETWORKS);
    jelli_prefs_drag(&prefs, -4096); /* The extremes allowed. */
    CHECK(prefs.top == JELLI_PREFS_MAX_NETWORKS - PREFS_LIST_ROWS && prefs.drag == 0);
    jelli_prefs_drag(&prefs, 4096);
    CHECK(prefs.top == 0u && prefs.drag == 0);
    jelli_prefs_drag(&prefs, -30);
    jelli_prefs_drag(&prefs, 30); /* Back where it began: nothing moved. */
    CHECK(prefs.top == 0u && prefs.drag == 0);
    prefs.screen = JELLI_PREFS_MENU;
    jelli_prefs_drag(&prefs, -200); /* Only the list scrolls. */
    CHECK(prefs.top == 0u && prefs.drag == 0);
}

int main(void)
{
    test_keys_truncate_to_the_capacity();
    test_each_page_has_its_keys_in_order();
    test_the_key_under_every_pixel();
    test_every_key_types_as_the_model_says();
    test_boxes_stop_at_their_edges();
    test_scrolling_conserves_the_drag();
    printf("Settings keyboard and list properties passed\n");
    return 0;
}

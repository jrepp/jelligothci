#include "prefs_internal.h"
#include <string.h>

/* Four rows of keys sized for a finger, inside the round screen: ten letters in the
 * widest row, a shift (or page key), seven characters, and delete in the third, and the
 * page key, space, and Join in the last. Together the pages reach every printable
 * ASCII character a WPA passphrase may hold. */

enum {
    ROW_Y = 124,
    ROW_PITCH = 46,
    KEY_HEIGHT = 42,
    KEY_WIDTH = 37,
    KEY_PITCH = 41,
    SIDE_KEY_WIDTH = 52, /* Shift, page keys, and delete beside the third row. */
    GAP = 4,
    PAGE_KEY_WIDTH = 70,
    SPACE_WIDTH = 150,
    JOIN_WIDTH = 110,
    BOTTOM_GAP = 6
};

static const char *const ROWS[PREFS_PAGE_COUNT][3] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    {"1234567890", "-/:;()$&@", ".,?!'\"_"},
    {"[]{}#%^*+=", "\\|~<>`", ".,?!'\"_"},
};

typedef struct {
    PrefsKey *keys;
    size_t count, capacity;
} Keys;

static void add(Keys *keys, PrefsKeyKind kind, char ch, int x, int y, int width)
{
    if (keys->count < keys->capacity)
        keys->keys[keys->count++] = (PrefsKey){kind, ch, {x, y, width, KEY_HEIGHT}};
}

static int chars_width(size_t count) { return count ? (int)count * KEY_PITCH - GAP : 0; }

static void add_chars(Keys *keys, const char *chars, int x, int y)
{
    for (size_t i = 0u; chars[i]; ++i)
        add(keys, PREFS_KEY_CHAR, chars[i], x + (int)i * KEY_PITCH, y, KEY_WIDTH);
}

static void add_centred_row(Keys *keys, const char *chars, int y)
{
    add_chars(keys, chars, PREFS_CENTER - chars_width(strlen(chars)) / 2, y);
}

static void add_third_row(Keys *keys, unsigned page, int y)
{
    const char *chars = ROWS[page][2];
    int width = SIDE_KEY_WIDTH + GAP + chars_width(strlen(chars)) + GAP + SIDE_KEY_WIDTH;
    int x = PREFS_CENTER - width / 2;
    bool letters = page == PREFS_LOWER || page == PREFS_UPPER;
    add(keys, letters ? PREFS_KEY_SHIFT : PREFS_KEY_MORE, 0, x, y, SIDE_KEY_WIDTH);
    add_chars(keys, chars, x + SIDE_KEY_WIDTH + GAP, y);
    add(keys, PREFS_KEY_DELETE, 0, x + width - SIDE_KEY_WIDTH, y, SIDE_KEY_WIDTH);
}

static void add_bottom_row(Keys *keys, unsigned page, int y)
{
    int width = PAGE_KEY_WIDTH + BOTTOM_GAP + SPACE_WIDTH + BOTTOM_GAP + JOIN_WIDTH;
    int x = PREFS_CENTER - width / 2;
    bool letters = page == PREFS_LOWER || page == PREFS_UPPER;
    add(keys, letters ? PREFS_KEY_DIGITS : PREFS_KEY_LETTERS, 0, x, y, PAGE_KEY_WIDTH);
    add(keys, PREFS_KEY_SPACE, ' ', x + PAGE_KEY_WIDTH + BOTTOM_GAP, y, SPACE_WIDTH);
    add(keys, PREFS_KEY_JOIN, 0, x + width - JOIN_WIDTH, y, JOIN_WIDTH);
}

size_t prefs_keys(unsigned page, PrefsKey *keys, size_t capacity)
{
    if (!keys || page >= PREFS_PAGE_COUNT)
        return 0u;
    Keys list = {keys, 0u, capacity};
    add_centred_row(&list, ROWS[page][0], ROW_Y);
    add_centred_row(&list, ROWS[page][1], ROW_Y + ROW_PITCH);
    add_third_row(&list, page, ROW_Y + 2 * ROW_PITCH);
    add_bottom_row(&list, page, ROW_Y + 3 * ROW_PITCH);
    return list.count;
}

bool prefs_key_at(unsigned page, int x, int y, PrefsKey *key)
{
    PrefsKey keys[PREFS_MAX_KEYS];
    size_t count = prefs_keys(page, keys, PREFS_MAX_KEYS);
    for (size_t i = 0u; i < count; ++i)
        if (prefs_inside(keys[i].box, x, y)) {
            if (key)
                *key = keys[i];
            return true;
        }
    return false;
}

static void append(JelliPrefs *prefs, char ch)
{
    if (prefs->password_size < JELLI_PREFS_PASSWORD_MAX && ch >= ' ' && ch <= '~') {
        prefs->password[prefs->password_size++] = ch;
        prefs->password[prefs->password_size] = '\0';
    }
}

bool prefs_type(JelliPrefs *prefs, const PrefsKey *key)
{
    prefs->dirty = true;
    switch (key->kind) {
    case PREFS_KEY_CHAR:
    case PREFS_KEY_SPACE:
        append(prefs, key->ch);
        return false;
    case PREFS_KEY_SHIFT:
        prefs->page = prefs->page == PREFS_LOWER ? PREFS_UPPER : PREFS_LOWER;
        return false;
    case PREFS_KEY_MORE:
        prefs->page = prefs->page == PREFS_DIGITS ? PREFS_SYMBOLS : PREFS_DIGITS;
        return false;
    case PREFS_KEY_DIGITS:
        prefs->page = PREFS_DIGITS;
        return false;
    case PREFS_KEY_LETTERS:
        prefs->page = PREFS_LOWER;
        return false;
    case PREFS_KEY_DELETE:
        if (prefs->password_size)
            prefs->password[--prefs->password_size] = '\0';
        return false;
    case PREFS_KEY_JOIN:
        return prefs->password_size >= JELLI_PREFS_PASSWORD_MIN;
    }
    return false;
}

void prefs_wipe(JelliPrefs *prefs)
{
    /* Through a volatile pointer, so the stores are never dropped as dead. */
    volatile char *password = prefs->password;
    for (size_t i = 0u; i < sizeof(prefs->password); ++i)
        password[i] = '\0';
    prefs->password_size = 0u;
    prefs->reveal = false;
}

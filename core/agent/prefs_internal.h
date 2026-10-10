#ifndef JELLI_AGENT_PREFS_INTERNAL_H
#define JELLI_AGENT_PREFS_INTERNAL_H

#include "jelli/agent_prefs.h"

/* Where everything of Settings sits on the 466x466 round screen. Taps and drawing
 * share these, so what is drawn is what is hit. Everything lies inside the circle. */

typedef struct {
    int x, y, width, height;
} PrefsBox;

typedef enum {
    PREFS_MENU_WIFI,
    PREFS_MENU_DONE,
    PREFS_SCAN_BACK,
    PREFS_LIST_BACK,
    PREFS_LIST_SCAN,
    PREFS_PASS_FIELD,
    PREFS_PASS_SHOW,
    PREFS_PASS_CANCEL,
    PREFS_RESULT_RETRY, /* Left of two: Try again. */
    PREFS_RESULT_BACK,  /* Right of two: Back. */
    PREFS_RESULT_ALONE, /* One button: OK or Back. */
    PREFS_PART_COUNT
} PrefsPart;

enum {
    PREFS_CENTER = 233,
    PREFS_LIST_Y = 102,   /* The first row's top. */
    PREFS_ROW_PITCH = 48, /* Rows move a whole row at a time. */
    PREFS_ROW_HEIGHT = 44,
    PREFS_LIST_ROWS = 5,
    PREFS_LIST_X = 63,
    PREFS_LIST_WIDTH = 340
};

PrefsBox prefs_box(PrefsPart part);
PrefsBox prefs_row_box(unsigned row); /* Row 0..PREFS_LIST_ROWS-1 on screen. */
bool prefs_inside(PrefsBox box, int x, int y);

/* The keyboard: four pages of keys, each in four rows. */
typedef enum { PREFS_LOWER, PREFS_UPPER, PREFS_DIGITS, PREFS_SYMBOLS, PREFS_PAGE_COUNT } PrefsPage;

typedef enum {
    PREFS_KEY_CHAR,
    PREFS_KEY_SHIFT,   /* Letters: capitals on or off. */
    PREFS_KEY_MORE,    /* Digits: to the other symbols; symbols: back to digits. */
    PREFS_KEY_DIGITS,  /* Letters: to digits and symbols. */
    PREFS_KEY_LETTERS, /* Digits or symbols: back to letters. */
    PREFS_KEY_DELETE,
    PREFS_KEY_SPACE,
    PREFS_KEY_JOIN
} PrefsKeyKind;

typedef struct {
    PrefsKeyKind kind;
    char ch; /* PREFS_KEY_CHAR: printable ASCII. */
    PrefsBox box;
} PrefsKey;

enum { PREFS_MAX_KEYS = 32 };

/* The keys of a page in drawing order; their count (at most PREFS_MAX_KEYS). */
size_t prefs_keys(unsigned page, PrefsKey *keys, size_t capacity);

/* The key at (x, y) on a page; false when there is none. */
bool prefs_key_at(unsigned page, int x, int y, PrefsKey *key);

/* Applies a key to the typed password; true for Join with a usable password. */
bool prefs_type(JelliPrefs *prefs, const PrefsKey *key);

/* Wipes the password, so no copy outlives its use. */
void prefs_wipe(JelliPrefs *prefs);

#endif

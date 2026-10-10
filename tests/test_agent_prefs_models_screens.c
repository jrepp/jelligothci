#include "../core/agent/prefs_draw.h" /* With agent_draw.h and agent_prefs.h. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The re-laid Settings menu and the model list drawn in the agent's real font (ADR-018),
 * checked by region: every pixel lies in the title, a card, a row, an arrow, or a
 * button; a change to the models changes only the Model card's pixels and a change to
 * Wi-Fi only the Wi-Fi card's; exactly the default's row is outlined, at every place in
 * the list; and the arrows show exactly when rows lie above or below the page. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: case %u: %s\n", __FILE__, __LINE__, current_case, #expr);      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SIDE = 466, GLYPH = JELLI_AGENT_GLYPH_HEIGHT };
static uint16_t pixels[SIDE * SIDE], before[SIDE * SIDE];
static unsigned current_case;

typedef struct {
    int top, bottom; /* Rows of pixels, bottom excluded. */
} Band;

static void draw(JelliPrefs *prefs, uint16_t *memory)
{
    JelliSurface surface = {memory, SIDE, SIDE, SIDE, {0u, 0u, 0u, 0u}};
    jelli_prefs_draw(prefs, &surface);
}

static bool in_box(PrefsBox box, int x, int y)
{
    return x >= box.x && y >= box.y && x < box.x + box.width && y < box.y + box.height;
}

static bool in_band(Band band, int y) { return y >= band.top && y < band.bottom; }

static JelliPrefsModels offer(unsigned count, unsigned current, size_t length)
{
    JelliPrefsModels models;
    memset(&models, 0, sizeof(models));
    for (unsigned i = 0u; i < count; ++i) {
        memset(models.names[i], 'W', length); /* The widest glyph, at its longest. */
        models.names[i][0] = (char)('A' + i);
    }
    models.count = (uint8_t)count;
    models.current = (uint8_t)current;
    return models;
}

static void opened(JelliPrefs *prefs, const JelliPrefsModels *models, bool online, bool wifi)
{
    jelli_prefs_init(prefs);
    jelli_prefs_open(prefs, (const uint8_t *)"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", wifi ? 32u : 0u,
                     wifi, 0u);
    CHECK(jelli_prefs_models(prefs, models, online));
}

/* Lit pixels of the menu lie in the title, the two cards, or Done. */
static void menu_is_confined(void)
{
    Band title = {58, 58 + 3 * GLYPH};
    for (int y = 0; y < SIDE; ++y)
        for (int x = 0; x < SIDE; ++x)
            if (pixels[y * SIDE + x] != PREFS_BLACK)
                CHECK(in_band(title, y) || in_box(prefs_box(PREFS_MENU_WIFI), x, y) ||
                      in_box(prefs_box(PREFS_MENU_MODEL), x, y) ||
                      in_box(prefs_box(PREFS_MENU_DONE), x, y));
}

/* Pixels that differ from before lie only in box. */
static void changed_only_in(PrefsBox box, bool changed)
{
    bool any = false;
    for (int y = 0; y < SIDE; ++y)
        for (int x = 0; x < SIDE; ++x)
            if (pixels[y * SIDE + x] != before[y * SIDE + x]) {
                CHECK(in_box(box, x, y));
                any = true;
            }
    CHECK(any == changed);
}

/* Each Model card state draws inside its card, and only its card differs from another;
 * Wi-Fi's state likewise touches only the Wi-Fi card. */
static void menu_regions(void)
{
    JelliPrefs prefs;
    JelliPrefsModels one = offer(1u, 0u, 40u), other = offer(2u, 1u, 3u);
    const JelliPrefsModels *lists[] = {NULL, NULL, &one, &other};
    const bool online[] = {false, true, true, false};
    for (current_case = 0u; current_case < 8u; ++current_case) {
        bool wifi = current_case >= 4u;
        opened(&prefs, NULL, false, wifi);
        draw(&prefs, before);
        unsigned state = current_case % 4u;
        opened(&prefs, lists[state], online[state], wifi);
        draw(&prefs, pixels);
        menu_is_confined();
        changed_only_in(prefs_box(PREFS_MENU_MODEL), state != 0u);
        memcpy(before, pixels, sizeof(pixels));
        opened(&prefs, lists[state], online[state], !wifi);
        draw(&prefs, pixels);
        menu_is_confined();
        changed_only_in(prefs_box(PREFS_MENU_WIFI), true);
    }
}

/* Lit pixels of the list lie in the title, a row with a model, an arrow, or Back. */
static void list_is_confined(const JelliPrefs *prefs)
{
    Band title = {46, 46 + 3 * GLYPH}, up = {88, 96}, down = {344, 352};
    unsigned shown = prefs->models.count - prefs->model_top;
    for (int y = 0; y < SIDE; ++y)
        for (int x = 0; x < SIDE; ++x) {
            if (pixels[y * SIDE + x] == PREFS_BLACK)
                continue;
            bool row = false;
            for (unsigned r = 0u; r < PREFS_LIST_ROWS && r < shown; ++r)
                row = row || in_box(prefs_row_box(r), x, y);
            CHECK(row || in_band(title, y) || in_box(prefs_box(PREFS_MODELS_BACK), x, y) ||
                  (in_band(up, y) && prefs->model_top) ||
                  (in_band(down, y) && prefs->model_top + PREFS_LIST_ROWS < prefs->models.count));
        }
}

static bool lit_in(Band band)
{
    for (int y = band.top; y < band.bottom; ++y)
        for (int x = 0; x < SIDE; ++x)
            if (pixels[y * SIDE + x] != PREFS_BLACK)
                return true;
    return false;
}

/* At every count and default, after opening and after scrolling to either end: only
 * the default's row is outlined, the arrows show the rows beyond the page, and the
 * page holds only rows of the list. */
static void list_regions(void)
{
    JelliPrefs prefs;
    for (unsigned count = 1u; count <= JELLI_PREFS_MAX_MODELS; ++count)
        for (unsigned current = 0u; current < count; ++current) {
            current_case = count * 100u + current;
            JelliPrefsModels models = offer(count, current, current % 2u ? 40u : 19u);
            opened(&prefs, &models, true, true);
            PrefsBox card = prefs_box(PREFS_MENU_MODEL);
            JelliPrefsJoin join;
            (void)jelli_prefs_tap(&prefs, card.x + 9, card.y + 9, 0u, &join);
            for (int end = 0; end < 3; ++end) {
                if (end)
                    jelli_prefs_drag(&prefs, (end == 1 ? 1 : -1) * PREFS_ROW_PITCH * 16);
                draw(&prefs, pixels);
                list_is_confined(&prefs);
                for (unsigned r = 0u; r < PREFS_LIST_ROWS && prefs.model_top + r < count; ++r) {
                    PrefsBox row = prefs_row_box(r);
                    uint16_t edge = pixels[(row.y + row.height / 2) * SIDE + row.x + 1];
                    bool is_current = prefs.model_top + r == current;
                    CHECK(edge == (is_current ? PREFS_LILAC : PREFS_KEY));
                }
                CHECK(lit_in((Band){88, 96}) == (prefs.model_top > 0u));
                CHECK(lit_in((Band){344, 352}) == (prefs.model_top + PREFS_LIST_ROWS < count));
            }
        }
}

int main(void)
{
    menu_regions();
    list_regions();
    printf("Settings model screen region checks passed\n");
    return 0;
}

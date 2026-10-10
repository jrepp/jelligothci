#include "../core/agent/prefs_draw.h" /* With agent_draw.h and agent_prefs.h. */
#include "prefs_bmp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The default model in Settings drawn with the agent's real font (ADR-018): the Model
 * card in each state, the list with its default outlined, names shortened to fit, and
 * seeded runs of lists, taps, and drags that never draw outside the round panel. Given a
 * folder, it writes each screen there as a BMP for a person to look at. */

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
static unsigned seed, step;
static uint32_t state = 0x1b873593u;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/* Draws over guard values: nothing outside the surface or the panel's circle, something
 * drawn, and nothing but dirty changed. */
static void draw_checked(JelliPrefs *prefs, uint16_t *memory)
{
    for (size_t i = 0u; i < (size_t)STRIDE * ROWS; ++i)
        memory[i] = GUARD;
    JelliSurface surface = {memory, SIDE, SIDE, STRIDE, {0u, 0u, 0u, 0u}};
    JelliPrefsModels models = prefs->models;
    JelliPrefsScreen screen = prefs->screen;
    uint8_t top = prefs->model_top;
    prefs->dirty = true;
    jelli_prefs_draw(prefs, &surface);
    CHECK(!prefs->dirty && surface.damage.width == SIDE && surface.damage.height == SIDE);
    CHECK(!memcmp(&models, &prefs->models, sizeof(models)) && screen == prefs->screen &&
          top == prefs->model_top);
    size_t lit = 0u;
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < STRIDE; ++x) {
            uint16_t pixel = memory[y * STRIDE + x];
            int64_t dx = x - 233, dy = y - 233;
            if (x >= SIDE || y >= SIDE) {
                CHECK(pixel == GUARD);
            } else if (pixel != 0u) {
                CHECK(dx * dx + dy * dy <= (int64_t)233 * 233);
                ++lit;
            }
        }
    CHECK(prefs->screen == JELLI_PREFS_CLOSED || lit > 500u);
}

static void shoot(JelliPrefs *prefs, const char *folder, const char *name)
{
    draw_checked(prefs, pixels);
    draw_checked(prefs, again);
    CHECK(!memcmp(pixels, again, sizeof(pixels))); /* The same state, the same pixels. */
    CHECK(!folder || prefs_bmp_save(folder, name, pixels, SIDE, STRIDE));
}

static JelliPrefsModels offer(const char *const *names, unsigned count, unsigned current)
{
    JelliPrefsModels models;
    memset(&models, 0, sizeof(models));
    for (unsigned i = 0u; i < count; ++i)
        (void)snprintf(models.names[i], sizeof(models.names[i]), "%s", names[i]);
    models.count = (uint8_t)count;
    models.current = (uint8_t)current;
    return models;
}

static void tap(JelliPrefs *prefs, PrefsBox box)
{
    JelliPrefsJoin join;
    (void)jelli_prefs_tap(prefs, box.x + box.width / 2, box.y + box.height / 2, 0u, &join);
}

static void opened(JelliPrefs *prefs, const JelliPrefsModels *models, bool online)
{
    jelli_prefs_init(prefs);
    jelli_prefs_open(prefs, (const uint8_t *)"Home network 5G", 15u, true, 0u);
    CHECK(jelli_prefs_models(prefs, models, online));
}

static uint16_t at(int x, int y) { return pixels[y * STRIDE + x]; }

static const char *const SIXTEEN[] = {"Claude",
                                      "gpt-oss:20b",
                                      "qwen3:30b-a3b",
                                      "llama3.1:8b",
                                      "hf.co/Org_1/Model-2:Q4+K",
                                      "mistral-small3.2:24b",
                                      "abcdefghijabcdefghijabcdefghijabcdefghij",
                                      "granite3.3:8b",
                                      "m08:x",
                                      "m09:x",
                                      "m10:x",
                                      "m11:x",
                                      "m12:x",
                                      "m13:x",
                                      "m14:x",
                                      "devstral:24b"};

/* The card in its three states (outlined lilac only when it opens a list), and the
 * list: the default outlined, others not, scrolled, and just picked. */
static void every_screen(const char *folder)
{
    JelliPrefs prefs;
    PrefsBox card = prefs_box(PREFS_MENU_MODEL);
    JelliPrefsModels two = offer(SIXTEEN, 2u, 1u);
    opened(&prefs, &two, true);
    shoot(&prefs, folder, "20-settings-model");
    CHECK(at(card.x + 1, card.y + card.height / 2) == PREFS_LILAC);
    opened(&prefs, NULL, false);
    shoot(&prefs, folder, "21-model-bridge-offline");
    CHECK(at(card.x + 1, card.y + card.height / 2) == PREFS_GREY);
    opened(&prefs, NULL, true);
    shoot(&prefs, folder, "22-model-none-offered");
    CHECK(at(card.x + 1, card.y + card.height / 2) == PREFS_GREY);
    opened(&prefs, &two, true);
    tap(&prefs, card);
    CHECK(prefs.screen == JELLI_PREFS_MODELS);
    shoot(&prefs, folder, "23-models");
    PrefsBox first = prefs_row_box(0u), second = prefs_row_box(1u);
    CHECK(at(first.x + 1, first.y + first.height / 2) == PREFS_KEY);
    CHECK(at(second.x + 1, second.y + second.height / 2) == PREFS_LILAC); /* The default. */
    JelliPrefsModels all = offer(SIXTEEN, 16u, 6u);
    opened(&prefs, &all, true);
    tap(&prefs, card);
    CHECK(prefs.model_top == 4u);
    shoot(&prefs, folder, "24-models-scrolled");
    tap(&prefs, prefs_row_box(0u));
    CHECK(prefs.models.current == 4u && prefs.screen == JELLI_PREFS_MODELS);
    shoot(&prefs, folder, "25-models-picked");
    CHECK(at(first.x + 1, first.y + first.height / 2) == PREFS_LILAC);
    jelli_prefs_drag(&prefs, PREFS_ROW_PITCH * 9);
    shoot(&prefs, folder, "26-models-top");
}

/* Two lists of one model each, the default, draw alike (on the list and on the card). */
static bool look_alike(const char *a, const char *b, bool list)
{
    JelliPrefs prefs;
    JelliPrefsModels models = offer(&a, 1u, 0u);
    opened(&prefs, &models, true);
    if (list)
        tap(&prefs, prefs_box(PREFS_MENU_MODEL));
    draw_checked(&prefs, pixels);
    memcpy(again, pixels, sizeof(again));
    models = offer(&b, 1u, 0u);
    CHECK(jelli_prefs_models(&prefs, &models, true));
    draw_checked(&prefs, pixels);
    return !memcmp(pixels, again, sizeof(pixels));
}

/* A row shows a name whole up to nineteen glyphs, else its first seventeen and ".."; the
 * card the same at seventeen and fifteen. */
static void names_are_shortened(void)
{
    CHECK(!look_alike("abcdefghijabcdefghi", "abcdefghijabcdefghX", true));
    CHECK(look_alike("abcdefghijabcdefgXYZ", "abcdefghijabcdefg/:+-.", true));
    CHECK(!look_alike("abcdefghijabcdefgXYZ", "abcdefghijabcdefXXYZ", true));
    CHECK(look_alike("abcdefghijabcdefghijabcdefghijabcdefghij", "abcdefghijabcdefg___", true));
    CHECK(!look_alike("abcdefghijabcdefg", "abcdefghijabcdefX", false));
    CHECK(look_alike("abcdefghijabcdeXYZ", "abcdefghijabcde/:+-.", false));
    CHECK(!look_alike("abcdefghijabcdeXYZ", "abcdefghijabcdXXYZ", false));
}

/* Any lists, taps, drags, opens, and closes draw inside the panel. */
static void fuzzed(void)
{
    for (seed = 0u; seed < 8u; ++seed) {
        JelliPrefs prefs;
        JelliPrefsJoin join;
        state = 0x85ebca6bu ^ (seed * 2654435761u);
        jelli_prefs_init(&prefs);
        jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
        for (step = 0u; step < 150u; ++step) {
            unsigned roll = next() % 10u;
            if (roll == 0u) {
                jelli_prefs_open(&prefs, NULL, 0u, next() & 1u, 0u);
            } else if (roll < 3u) {
                unsigned count = next() % 17u;
                JelliPrefsModels models = offer(SIXTEEN, count, count ? next() % count : 0u);
                CHECK(jelli_prefs_models(&prefs, &models, next() & 1u));
            } else if (roll < 5u) {
                jelli_prefs_drag(&prefs, (int)(next() % 400u) - 200);
            } else {
                PrefsBox box = next() % 3u ? prefs_row_box(next() % PREFS_LIST_ROWS)
                                           : prefs_box((PrefsPart)(next() % PREFS_PART_COUNT));
                (void)jelli_prefs_tap(&prefs, box.x + (int)(next() % (unsigned)box.width),
                                      box.y + (int)(next() % (unsigned)box.height), 0u, &join);
            }
            if (prefs.screen == JELLI_PREFS_MODELS)
                CHECK(prefs.models.count && prefs.models.current < prefs.models.count &&
                      prefs.model_top + PREFS_LIST_ROWS <= (prefs.models.count > PREFS_LIST_ROWS
                                                                ? prefs.models.count
                                                                : PREFS_LIST_ROWS));
            draw_checked(&prefs, pixels);
        }
    }
    seed = step = 0u;
}

int main(int argc, char **argv)
{
    const char *folder = argc > 1 ? argv[1] : NULL;
    every_screen(folder);
    names_are_shortened();
    fuzzed();
    printf("Settings model drawing checks passed%s%s\n", folder ? "; previews in " : "",
           folder ? folder : "");
    return 0;
}

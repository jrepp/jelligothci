#include "../core/agent/prefs_draw.h"
#include "jelli/agent_draw.h"
#include "jelli/assets.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Loki Agent's text (ADR-016), white-box: every glyph at every scale stays in its cell
 * and is its scale-one self magnified, bytes outside printable ASCII draw as '?', long
 * text stops at 64 glyphs, centring is by text_width, and the lowercase letters are
 * real lowercase: in the capitals' columns, distinct from them and each other, with an
 * x-height from row four, a common baseline on row eight, and two-row descenders only
 * for g, j, p, q, and y. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: case %u: %s\n", __FILE__, __LINE__, which, #expr);             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { WIDTH = 600, HEIGHT = 110, GUARD = 0x5a5a, INK = 0x1234, X = 7, Y = 5 };

static unsigned which;
static uint16_t pixels[WIDTH * HEIGHT], reference[WIDTH * HEIGHT];

static JelliSurface cleared(uint16_t *memory)
{
    for (size_t i = 0u; i < (size_t)WIDTH * HEIGHT; ++i)
        memory[i] = GUARD;
    return (JelliSurface){memory, WIDTH, HEIGHT, WIDTH, {0u, 0u, 0u, 0u}};
}

static bool ink(const uint16_t *memory, int x, int y) { return memory[y * WIDTH + x] == INK; }

static void draw(uint16_t *memory, int x, const char *text, size_t length, unsigned scale)
{
    JelliSurface surface = cleared(memory);
    jelli_agent_text(&surface, x, Y, text, length, scale, INK);
}

/* A glyph at scale s paints its scale-one pixels as s-by-s blocks, inside its cell. */
static void check_magnified(unsigned effective)
{
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x) {
            int cx = x - X, cy = y - Y, s = (int)effective;
            bool cell = cx >= 0 && cy >= 0 && cx < 8 * s && cy < 12 * s;
            bool want = cell && ink(reference, X + cx / s, Y + cy / s);
            CHECK(ink(pixels, x, y) == want);
        }
}

static void test_every_glyph_at_every_scale(void)
{
    for (int code = 32; code <= 126; ++code) {
        char ch = (char)code;
        draw(reference, X, &ch, 1u, 1u);
        for (unsigned scale = 0u; scale <= 10u; ++scale) {
            which = (unsigned)code * 100u + scale;
            unsigned effective = scale == 0u ? 1u : scale > 8u ? 8u : scale;
            draw(pixels, X, &ch, 1u, scale);
            check_magnified(effective);
        }
    }
}

/* What draws for a byte outside 32..126 is '?', and a run of text is its glyphs side by
 * side, eight pixels times the scale apart. */
static void test_unprintable_bytes_and_advances(void)
{
    for (int code = 0; code < 256; ++code) {
        which = (unsigned)code;
        if (code >= 32 && code <= 126)
            continue;
        char ch = (char)code;
        draw(reference, X, "?", 1u, 2u);
        draw(pixels, X, &ch, 1u, 2u);
        CHECK(!memcmp(pixels, reference, sizeof(pixels)));
    }
    const char text[] = "Ab9 gy";
    for (unsigned scale = 1u; scale <= 3u; ++scale) {
        which = scale;
        draw(pixels, X, text, 6u, scale);
        JelliSurface surface = cleared(reference);
        for (int i = 0; i < 6; ++i)
            jelli_agent_text(&surface, X + i * 8 * (int)scale, Y, text + i, 1u, scale, INK);
        CHECK(!memcmp(pixels, reference, sizeof(pixels)));
        CHECK(jelli_agent_text_width(6u, scale) == 6 * 8 * (int)scale);
    }
}

/* Past 64 glyphs nothing more draws; centring places text at cx minus half its width. */
static void test_long_text_and_centring(void)
{
    char text[80];
    for (size_t i = 0u; i < sizeof(text); ++i)
        text[i] = (char)('A' + i % 26u);
    which = 64u;
    draw(pixels, 0, text, 80u, 1u);
    draw(reference, 0, text, 64u, 1u);
    CHECK(!memcmp(pixels, reference, sizeof(pixels)));
    CHECK(jelli_agent_text_width(80u, 1u) == 512 && jelli_agent_text_width(0u, 4u) == 0);
    for (unsigned length = 0u; length <= 9u; ++length)
        for (unsigned scale = 0u; scale <= 9u; ++scale) {
            which = length * 10u + scale;
            JelliSurface surface = cleared(pixels);
            jelli_agent_text_centered(&surface, 300, Y, text, length, scale, INK);
            draw(reference, 300 - jelli_agent_text_width(length, scale) / 2, text, length, scale);
            CHECK(!memcmp(pixels, reference, sizeof(pixels)));
        }
    which = 0u;
    draw(pixels, X, NULL, 5u, 1u); /* No text: nothing drawn. */
    draw(reference, X, "", 0u, 1u);
    CHECK(!memcmp(pixels, reference, sizeof(pixels)));
}

static bool rows_equal(const uint8_t *a, const uint8_t *b)
{
    return !memcmp(a, b, JELLI_AGENT_GLYPH_HEIGHT);
}

/* The rows and columns a glyph's pixels use. */
static void extent(const uint8_t *rows, unsigned *columns, unsigned *lit_rows)
{
    *columns = *lit_rows = 0u;
    for (unsigned row = 0u; row < JELLI_AGENT_GLYPH_HEIGHT; ++row) {
        *columns |= rows[row];
        if (rows[row])
            *lit_rows |= 1u << row;
    }
}

static void test_lowercase_letters_look_lowercase(void)
{
    unsigned font_columns = 0u, ignored = 0u;
    for (int code = 'A'; code <= 'Z'; ++code) {
        unsigned columns = 0u;
        extent(jelli_asset_lookup_glyph(NULL, (uint8_t)code), &columns, &ignored);
        font_columns |= columns;
    }
    for (int code = 0; code < 256; ++code) {
        which = (unsigned)code;
        CHECK((jelli_agent_lowercase((uint8_t)code) != NULL) == (code >= 'a' && code <= 'z'));
    }
    for (int letter = 'a'; letter <= 'z'; ++letter) {
        which = (unsigned)letter;
        const uint8_t *rows = jelli_agent_lowercase((uint8_t)letter);
        const uint8_t *capital = jelli_asset_lookup_glyph(NULL, (uint8_t)(letter - 'a' + 'A'));
        unsigned columns = 0u, lit = 0u;
        extent(rows, &columns, &lit);
        bool descends = strchr("gjpqy", letter) != NULL, tall = strchr("bdfhijklt", letter) != NULL;
        CHECK(columns && (columns & ~font_columns) == 0u); /* The capitals' five columns. */
        CHECK(!rows_equal(rows, capital));
        CHECK((lit & 0x803u) == 0u);                  /* Rows 0, 1, 11 stay empty. */
        CHECK(((lit & 0x600u) != 0u) == descends);    /* Only g j p q y go below row 8... */
        CHECK(!descends || (lit & 0x600u) == 0x600u); /* ...and they go two rows. */
        CHECK((lit & 0x10u) != 0u);                   /* Every x-height starts at row 4. */
        CHECK(((lit & 0xcu) != 0u) == tall);          /* Ascenders and dots use rows 2-3. */
        CHECK((lit & 0x100u) != 0u);                  /* All sit on the baseline, row 8. */
        for (int other = 'a'; other < letter; ++other)
            CHECK(!rows_equal(rows, jelli_agent_lowercase((uint8_t)other)));
    }
}

/* What draws for 'a'..'z' is the lowercase table, and for capitals the pets' font. */
static void test_drawn_letters_use_the_right_table(void)
{
    for (int code = 'A'; code <= 'z'; ++code) {
        which = (unsigned)code;
        if (code > 'Z' && code < 'a')
            continue;
        char ch = (char)code;
        draw(pixels, X, &ch, 1u, 1u);
        const uint8_t *rows = code >= 'a' ? jelli_agent_lowercase((uint8_t)code)
                                          : jelli_asset_lookup_glyph(NULL, (uint8_t)code);
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 8; ++x)
                CHECK(ink(pixels, X + x, Y + y) == (((rows[y] >> (7 - x)) & 1u) != 0u));
    }
}

/* RGB565 keeps each channel's top bits; the Settings palette is what its comments say. */
static void test_colours(void)
{
    for (unsigned value = 0u; value < 256u; ++value) {
        which = value;
        CHECK(jelli_agent_rgb(value, 0u, 0u) == (uint16_t)((value >> 3) << 11));
        CHECK(jelli_agent_rgb(0u, value, 0u) == (uint16_t)((value >> 2) << 5));
        CHECK(jelli_agent_rgb(0u, 0u, value) == (uint16_t)(value >> 3));
        CHECK(jelli_agent_rgb(value | 0x100u, value | 0xf00u, value | 0x300u) ==
              jelli_agent_rgb(value, value, value));
    }
    CHECK(jelli_agent_rgb(255u, 255u, 255u) == PREFS_WHITE);
    CHECK(jelli_agent_rgb(200u, 162u, 255u) == PREFS_LILAC);
    CHECK(jelli_agent_rgb(140u, 140u, 150u) == PREFS_GREY);
    CHECK(jelli_agent_rgb(44u, 44u, 56u) == PREFS_KEY);
    CHECK(jelli_agent_rgb(30u, 30u, 38u) == PREFS_KEY_DIM);
    CHECK(jelli_agent_rgb(255u, 140u, 112u) == PREFS_WARM);
}

int main(void)
{
    test_every_glyph_at_every_scale();
    test_unprintable_bytes_and_advances();
    test_long_text_and_centring();
    test_lowercase_letters_look_lowercase();
    test_drawn_letters_use_the_right_table();
    test_colours();
    printf("Agent text checks passed\n");
    return 0;
}

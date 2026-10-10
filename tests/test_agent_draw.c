#include "jelli/agent_draw.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Loki Agent's flat drawing (ADR-016), white-box: a fill paints exactly its clipped
 * rectangle, a rounded rectangle is symmetric convex rows, an outline is the rim of the
 * same shape with no gaps (all of it once thickness reaches half), and anything drawn on
 * a small surface is the matching window of the same drawing on a larger one, so
 * clipping at all four edges loses nothing and adds nothing. Seeded; failures name the
 * case. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: case %u: %s\n", __FILE__, __LINE__, which, #expr);             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { WIDTH = 64, HEIGHT = 48, STRIDE = 72, ROWS = 52, GUARD = 0x5a5a, INK = 0x1234 };
enum { BIG_WIDTH = 200, BIG_HEIGHT = 160, OFFSET_X = 60, OFFSET_Y = 50 };

static unsigned which;
static uint32_t state;
static uint16_t pixels[STRIDE * ROWS], big[BIG_WIDTH * BIG_HEIGHT];

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static int between(int low, int high) { return low + (int)(next() % (uint32_t)(high - low + 1)); }

static JelliSurface small_surface(void)
{
    for (size_t i = 0u; i < sizeof(pixels) / sizeof(pixels[0]); ++i)
        pixels[i] = GUARD;
    return (JelliSurface){pixels, WIDTH, HEIGHT, STRIDE, {0u, 0u, 0u, 0u}};
}

static bool ink(int x, int y) { return pixels[y * STRIDE + x] == INK; }

/* Random rectangles, many straddling an edge or off it, and some extreme. */
static void test_fill_paints_exactly_its_clipped_rectangle(void)
{
    static const int extremes[] = {-2147483647 - 1, -1, 0, 2147483647};
    for (which = 0u; which < 3000u; ++which) {
        state = 0x51ed2701u ^ which;
        int x = between(-80, 140), y = between(-80, 120), w = between(-10, 150),
            h = between(-10, 120);
        if (which % 7u == 0u) {
            x = extremes[next() % 4u];
            w = extremes[next() % 4u];
        }
        JelliSurface surface = small_surface();
        jelli_agent_fill(&surface, x, y, w, h, INK);
        for (int row = 0; row < ROWS; ++row)
            for (int column = 0; column < STRIDE; ++column) {
                bool inside = column < WIDTH && row < HEIGHT && w > 0 && h > 0 &&
                              (int64_t)column >= x && (int64_t)column < (int64_t)x + w &&
                              row >= y && (int64_t)row < (int64_t)y + h;
                CHECK(pixels[row * STRIDE + column] == (inside ? INK : GUARD));
            }
    }
}

/* Nothing of a shape at (2, 2) lands outside its box, even when it is thicker than
 * the box is wide. */
static void check_only_inside(int w, int h)
{
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < STRIDE; ++x)
            if (x < 2 || y < 2 || x >= 2 + w || y >= 2 + h)
                CHECK(pixels[y * STRIDE + x] == GUARD);
}

/* The painted span of a row of the shape at (2, 2): first column and width. */
static void span(int row, int width, int *first, int *count)
{
    *first = -1;
    *count = 0;
    for (int column = 0; column < width; ++column)
        if (ink(2 + column, 2 + row)) {
            if (*first < 0)
                *first = column;
            ++*count;
        }
}

/* Rows of a rounded rectangle: one centred run each, never empty, insets shrinking to
 * the middle, the same top and bottom; corners cut once the radius reaches two. */
static void check_round_rect(int w, int h, int radius)
{
    int previous = 0;
    for (int row = 0; row < h; ++row) {
        int first = 0, count = 0, mirror_first = 0, mirror_count = 0;
        span(row, w, &first, &count);
        span(h - 1 - row, w, &mirror_first, &mirror_count);
        CHECK(count >= 1 && first >= 0 && count == w - 2 * first); /* Centred and whole. */
        CHECK(first == mirror_first && count == mirror_count);
        if (2 * row < h)
            CHECK(row == 0 || first <= previous);
        previous = first;
        CHECK(radius > 0 || first == 0);
        CHECK(!ink(1, 2 + row) && !ink(2 + w, 2 + row));
    }
    int effective = radius < (w < h ? w : h) / 2 ? radius : (w < h ? w : h) / 2;
    CHECK((effective >= 2) == !ink(2, 2));
    for (int column = 0; column < WIDTH; ++column) /* Nothing outside the box. */
        CHECK(!ink(column, 1) && !ink(column, 2 + h));
}

static void test_round_rect_rows_at_every_small_size(void)
{
    which = 0u;
    for (int w = 1; w <= 24; ++w)
        for (int h = 1; h <= 24; ++h)
            for (int radius = -2; radius <= 14; ++radius, ++which) {
                JelliSurface surface = small_surface();
                jelli_agent_round_rect(&surface, 2, 2, w, h, radius, INK);
                check_round_rect(w, h, radius);
                /* A radius beyond half the shorter side draws as that half. */
                uint16_t shape[STRIDE * ROWS];
                memcpy(shape, pixels, sizeof(shape));
                surface = small_surface();
                jelli_agent_round_rect(&surface, 2, 2, w, h, 1000, INK);
                uint16_t huge[STRIDE * ROWS];
                memcpy(huge, pixels, sizeof(huge));
                surface = small_surface();
                jelli_agent_round_rect(&surface, 2, 2, w, h, (w < h ? w : h) / 2, INK);
                CHECK(!memcmp(huge, pixels, sizeof(huge)));
                check_only_inside(w, h);
                CHECK(radius < (w < h ? w : h) / 2 || !memcmp(shape, pixels, sizeof(shape)));
            }
}

static bool filled_at(const uint16_t *filled, int x, int y, int w, int h)
{
    return x >= 0 && y >= 0 && x < w && y < h && filled[(2 + y) * STRIDE + 2 + x] == INK;
}

/* The outline lies in the filled shape, keeps every pixel on its edge, and mirrors. */
static void check_outline(const uint16_t *filled, int w, int h, int thickness)
{
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            bool on = ink(2 + x, 2 + y), in = filled_at(filled, x, y, w, h);
            bool edge =
                in && (!filled_at(filled, x - 1, y, w, h) || !filled_at(filled, x + 1, y, w, h) ||
                       !filled_at(filled, x, y - 1, w, h) || !filled_at(filled, x, y + 1, w, h));
            CHECK(!on || in);
            CHECK(!edge || on); /* No gap in the ring. */
            CHECK(on == ink(2 + w - 1 - x, 2 + y) && on == ink(2 + x, 2 + h - 1 - y));
            if (2 * thickness >= (w < h ? w : h)) /* Thick enough to fill it. */
                CHECK(on == in);
        }
    if (w > 2 * thickness && h > 2 * thickness) /* Otherwise it has a hole. */
        CHECK(!ink(2 + w / 2, 2 + h / 2));
    check_only_inside(w, h);
}

static void test_round_outline_is_the_rim_of_its_rounded_rect(void)
{
    uint16_t filled[STRIDE * ROWS];
    which = 0u;
    for (int w = 1; w <= 22; ++w)
        for (int h = 1; h <= 22; ++h)
            for (int radius = 0; radius <= 12; radius += (radius < 4 ? 1 : 3))
                for (int thickness = 1; thickness <= 13; ++thickness, ++which) {
                    JelliSurface surface = small_surface();
                    jelli_agent_round_rect(&surface, 2, 2, w, h, radius, INK);
                    memcpy(filled, pixels, sizeof(filled));
                    surface = small_surface();
                    jelli_agent_round_outline(&surface, 2, 2, w, h, radius, thickness, INK);
                    check_outline(filled, w, h, thickness);
                }
}

/* With no rounding, an outline is exactly a frame thickness pixels wide. */
static void test_square_outline_is_an_exact_frame(void)
{
    which = 0u;
    for (int w = 1; w <= 30; ++w)
        for (int h = 1; h <= 30; ++h)
            for (int t = 1; t <= 16; ++t, ++which) {
                JelliSurface surface = small_surface();
                jelli_agent_round_outline(&surface, 2, 2, w, h, 0, t, INK);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        CHECK(ink(2 + x, 2 + y) == (x < t || x >= w - t || y < t || y >= h - t));
            }
}

static void test_degenerate_shapes_draw_nothing(void)
{
    which = 0u;
    JelliSurface surface = small_surface();
    jelli_agent_round_rect(&surface, 2, 2, 0, 10, 3, INK);
    jelli_agent_round_rect(&surface, 2, 2, 10, -1, 3, INK);
    jelli_agent_round_rect(&surface, 0, 0, 4097, 10, 3, INK); /* Beyond any screen. */
    jelli_agent_round_outline(&surface, 2, 2, 10, 10, 3, 0, INK);
    jelli_agent_round_outline(&surface, 2, 2, 10, 10, 3, -5, INK);
    jelli_agent_round_outline(&surface, 2, 2, 0, 10, 3, 2, INK);
    jelli_agent_round_outline(&surface, 0, 0, 10, 4097, 3, 2, INK);
    for (size_t i = 0u; i < sizeof(pixels) / sizeof(pixels[0]); ++i)
        CHECK(pixels[i] == GUARD);
    jelli_agent_round_rect(&surface, 5, 6, 1, 1, 9, INK); /* One pixel, any radius. */
    jelli_agent_round_outline(&surface, 9, 9, 1, 1, 9, 9, INK);
    CHECK(ink(5, 6) && ink(9, 9) && !ink(6, 6) && !ink(10, 9) && !ink(9, 10));
    JelliSurface empty = {NULL, WIDTH, HEIGHT, STRIDE, {0u, 0u, 0u, 0u}};
    jelli_agent_round_rect(&empty, 0, 0, 5, 5, 1, INK);
    jelli_agent_round_outline(&empty, 0, 0, 5, 5, 1, 1, INK);
    jelli_agent_round_rect(NULL, 0, 0, 5, 5, 1, INK);
    jelli_agent_round_outline(NULL, 0, 0, 5, 5, 1, 1, INK);
    jelli_agent_text(NULL, 0, 0, "A", 1u, 1u, INK);
    jelli_agent_text_centered(&empty, 0, 0, "A", 1u, 1u, INK);
}

/* One random drawing at (x, y) on surface. */
static void draw_random(JelliSurface *surface, const uint32_t *values, int x, int y)
{
    static const char text[] = "Wi-Fi gjpqy \x01\xff~";
    int w = (int)(values[1] % 90u), h = (int)(values[2] % 70u);
    int radius = (int)(values[3] % 40u) - 2, thickness = (int)(values[4] % 20u);
    switch (values[0] % 4u) {
    case 0:
        jelli_agent_fill(surface, x, y, w, h, INK);
        break;
    case 1:
        jelli_agent_round_rect(surface, x, y, w, h, radius, INK);
        break;
    case 2:
        jelli_agent_round_outline(surface, x, y, w, h, radius, thickness, INK);
        break;
    default:
        jelli_agent_text(surface, x, y, text + values[1] % 8u, values[2] % 10u, values[3] % 10u,
                         INK);
        break;
    }
}

static void test_small_surface_is_a_window_on_a_larger_one(void)
{
    for (which = 0u; which < 2500u; ++which) {
        state = 0x0dd5eedu ^ (which * 2654435761u);
        const uint32_t values[5] = {next(), next(), next(), next(), next()};
        int x = between(-70, WIDTH + 4), y = between(-60, HEIGHT + 4);
        JelliSurface surface = small_surface();
        draw_random(&surface, values, x, y);
        for (size_t i = 0u; i < sizeof(big) / sizeof(big[0]); ++i)
            big[i] = GUARD;
        JelliSurface large = {big, BIG_WIDTH, BIG_HEIGHT, BIG_WIDTH, {0u, 0u, 0u, 0u}};
        draw_random(&large, values, x + OFFSET_X, y + OFFSET_Y);
        for (int row = 0; row < ROWS; ++row)
            for (int column = 0; column < STRIDE; ++column) {
                uint16_t want = column < WIDTH && row < HEIGHT
                                    ? big[(row + OFFSET_Y) * BIG_WIDTH + column + OFFSET_X]
                                    : (uint16_t)GUARD;
                CHECK(pixels[row * STRIDE + column] == want);
            }
    }
}

int main(void)
{
    test_fill_paints_exactly_its_clipped_rectangle();
    test_round_rect_rows_at_every_small_size();
    test_round_outline_is_the_rim_of_its_rounded_rect();
    test_square_outline_is_an_exact_frame();
    test_degenerate_shapes_draw_nothing();
    test_small_surface_is_a_window_on_a_larger_one();
    printf("Agent drawing checks passed\n");
    return 0;
}

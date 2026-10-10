#include "../tools/bench/rect_workload.h"
#include <stdio.h>
#include <string.h>

enum { STRIDE = 473, COUNT = STRIDE * JELLI_HEIGHT, GUARD = 16 };
static uint16_t expected[COUNT + 2 * GUARD];
static uint16_t actual[COUNT + 2 * GUARD];

static bool check(unsigned kind, bool dim, bool clipped)
{
    for (unsigned i = 0; i < COUNT + 2u * GUARD; ++i)
        expected[i] = actual[i] = (uint16_t)(i * 7u);
    JelliSurface a = {.pixels = actual + GUARD, .width = 466, .height = 466, .stride = STRIDE};
    JelliSurface b = a;
    b.pixels = expected + GUARD;
    Canvas ca = {
        &a,  clipped ? 17 : 0, clipped ? 31 : 0, clipped ? 450 : 466, clipped ? 457 : 466, 0, dim,
        NULL};
    Canvas cb = ca;
    cb.s = &b;
    jelli_rect_workload(&ca, kind, jelli_canvas_rect);
    jelli_rect_workload(&cb, kind, jelli_rect_reference);
    for (int y = -6; y < 472; ++y) {
        int x = (y + 6) * 37 % 478 - 6;
        jelli_canvas_rect(&ca, x, y, 0, 6, 0xffffu);
        jelli_rect_reference(&cb, x, y, 0, 6, 0xffffu);
        jelli_canvas_rect(&ca, x, y, 6, 0, 0xffffu);
        jelli_rect_reference(&cb, x, y, 6, 0, 0xffffu);
        jelli_canvas_rect(&ca, x, y, 1, 1, 0x1234u);
        jelli_rect_reference(&cb, x, y, 1, 1, 0x1234u);
    }
    return memcmp(actual, expected, sizeof(actual)) == 0;
}

int main(void)
{
    for (unsigned kind = 0; kind < 3u; ++kind)
        for (unsigned flags = 0; flags < 4u; ++flags)
            if (!check(kind, (flags & 1u) != 0u, (flags & 2u) != 0u)) {
                fprintf(stderr, "Rectangle mismatch: kind=%u flags=%u\n", kind, flags);
                return 1;
            }
    puts("Rectangle reference equivalence passed, including padding and guards");
    return 0;
}

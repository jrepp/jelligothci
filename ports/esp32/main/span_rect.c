#include "span_rect.h"
#include "span_kernels.h"

/* Engine-task-only diagnostic selector; both trials execute the same rectangle
 * machine code. The long-span call target is the sole treatment variable. */
static void (*long_fill)(uint16_t *, size_t, uint16_t, unsigned) = jelli_span_fill_scalar;
void jelli_span_rect_mode(bool simd)
{
    long_fill = simd ? jelli_span_fill_simd : jelli_span_fill_scalar;
}

static bool in_round(int x, int y)
{
    int dx = 2 * x - 465, dy = 2 * y - 465;
    return dx * dx + dy * dy <= 466 * 466;
}

/* Diagnostic candidate: identical clipping/dimming to the current renderer.
 * Keep tiny spans inline; calling a general SIMD wrapper per glyph row costs. */
void jelli_canvas_rect_simd(Canvas *c, int x, int y, int w, int h, uint16_t color)
{
    int right = x + w < c->right ? x + w : c->right;
    int bottom = y + h < c->bottom ? y + h : c->bottom;
    if (x < c->left)
        x = c->left;
    if (y < c->top)
        y = c->top;
    if (c->dim)
        color = (uint16_t)((color >> 1) & 0x7befu);
    for (int row = y; row < bottom; ++row) {
        int left = x, end = right;
        while (left < end && !in_round(left, row))
            ++left;
        while (end > left && !in_round(end - 1, row))
            --end;
        /* The loop also ends when in_round becomes true, with end > left. */
        // cppcheck-suppress knownConditionTrueFalse
        if (end - left >= 64)
            long_fill(c->s->pixels + (unsigned)row * c->s->stride + (unsigned)left,
                      (size_t)(end - left), color, 16);
        else
            for (int column = left; column < end; ++column)
                c->s->pixels[(unsigned)row * c->s->stride + (unsigned)column] = color;
    }
}

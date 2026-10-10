#ifndef JELLI_RECT_WORKLOAD_H
#define JELLI_RECT_WORKLOAD_H
#include "../../core/pet_canvas.h"

typedef void (*JelliRectDraw)(Canvas *, int, int, int, int, uint16_t);

/* Keep the original pixel helper in this translation unit too: the original
 * renderer could inline it. An external call would bias the baseline slower. */
static void jelli_rect_reference_pixel(Canvas *c, int x, int y, uint16_t color)
{
    if (c->dim)
        color = (uint16_t)((color >> 1) & 0x7befu);
    int dx = 2 * x - 465, dy = 2 * y - 465;
    if (x >= c->left && y >= c->top && x < c->right && y < c->bottom &&
        dx * dx + dy * dy <= 466 * 466)
        c->s->pixels[(unsigned)y * c->s->stride + (unsigned)x] = color;
}

/* Original implementation retained as the experiment's scalar reference. */
static void jelli_rect_reference(Canvas *c, int x, int y, int w, int h, uint16_t color)
{
    int right = x + w < c->right ? x + w : c->right;
    int bottom = y + h < c->bottom ? y + h : c->bottom;
    if (x < c->left)
        x = c->left;
    if (y < c->top)
        y = c->top;
    for (int row = y; row < bottom; ++row)
        for (int column = x; column < right; ++column)
            jelli_rect_reference_pixel(c, column, row, color);
}

/* Representative sixfold sprite pixels, broad fill, and clipped edge blocks. */
static void jelli_rect_workload(Canvas *c, unsigned kind, JelliRectDraw draw)
{
    if (kind == 1u) {
        draw(c, 0, 0, 466, 466, 0xabcdu);
        return;
    }
    for (unsigned i = 0; i < 1024u; ++i) {
        int x = kind ? (int)(i * 37u % 500u) - 17 : 130 + (int)(i % 32u) * 6;
        int y = kind ? (int)(i * 53u % 500u) - 17 : 130 + (int)(i / 32u) * 6;
        draw(c, x, y, 6, 6, (uint16_t)(i * 17u));
    }
}
#endif

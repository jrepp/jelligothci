#include "pet_canvas.h"
#include "pet_draw.h"
#include <string.h>
bool jelli_canvas_in_round(int x, int y)
{
    int dx = 2 * x - 465, dy = 2 * y - 465;
    return dx * dx + dy * dy <= 466 * 466;
}
void jelli_canvas_pixel(Canvas *c, int x, int y, uint16_t color)
{
    if (c->dim)
        color = (uint16_t)((color >> 1) & 0x7befu);
    if (x >= c->left && y >= c->top && x < c->right && y < c->bottom && jelli_canvas_in_round(x, y))
        c->s->pixels[(unsigned)y * c->s->stride + (unsigned)x] = color;
}
void jelli_canvas_rect(Canvas *c, int x, int y, int w, int h, uint16_t color)
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
        /* A circle intersects each row in one contiguous interval. */
        while (left < end && !jelli_canvas_in_round(left, row))
            ++left;
        while (end > left && !jelli_canvas_in_round(end - 1, row))
            --end;
        for (int column = left; column < end; ++column)
            c->s->pixels[(unsigned)row * c->s->stride + (unsigned)column] = color;
    }
}
void jelli_canvas_disk(Canvas *c, int x, int y, int radius, uint16_t color)
{
    int left = x - radius < c->left ? c->left - x : -radius;
    int right = x + radius >= c->right ? c->right - x - 1 : radius;
    int top = y - radius < c->top ? c->top - y : -radius;
    int bottom = y + radius >= c->bottom ? c->bottom - y - 1 : radius;
    for (int row = top; row <= bottom; ++row)
        for (int column = left; column <= right; ++column)
            if (row * row + column * column <= radius * radius)
                jelli_canvas_pixel(c, x + column, y + row, color);
}
void jelli_canvas_text(Canvas *c, const char *value, int x, int y, unsigned scale, uint16_t color)
{
    while (*value) {
        const uint8_t *rows = jelli_asset_lookup_glyph(c->assets, (uint8_t)*value++);
        for (unsigned row = 0; row < 12u; ++row)
            for (unsigned column = 0; column < 8u; ++column)
                if (rows[row] & (uint8_t)(1u << (7u - column)))
                    jelli_canvas_rect(c, x + (int)(column * scale), y + (int)(row * scale),
                                      (int)scale, (int)scale, color);
        x += (int)(8u * scale);
    }
}
void jelli_canvas_centered(Canvas *c, const char *value, int y, unsigned scale, uint16_t color)
{
    int width = (int)(strlen(value) * 8u * scale);
    jelli_canvas_text(c, value, 233 - width / 2, y, scale, color);
}
void jelli_canvas_sprite(Canvas *c, uint32_t id, int x, int y, unsigned scale)
{
    const JelliAsset *a = jelli_asset_lookup(c->assets, id);
    if (!a || !a->pixels || !a->mask)
        return;
    if (x >= c->right || y >= c->bottom || x + (int)(a->width * scale) <= c->left ||
        y + (int)(a->height * scale) <= c->top)
        return;
    for (unsigned row = 0; row < a->height; ++row)
        for (unsigned column = 0; column < a->width; ++column)
            if (a->mask[row * a->mask_stride + column / 8u] & (uint8_t)(1u << (7u - column % 8u)))
                jelli_canvas_rect(
                    c, x + (int)(column * scale), y + (int)(row * scale), (int)scale, (int)scale,
                    jelli_pet_night_color(a->pixels[row * a->width + column], c->icon_night));
}

void jelli_canvas_centered_sprite(Canvas *c, uint32_t id, int x, int y, unsigned scale)
{
    const JelliAsset *a = jelli_asset_lookup(c->assets, id);
    if (!a)
        return;
    jelli_canvas_sprite(c, id, x - (int)((a->centroid_x_q8 * scale + 128u) / 256u),
                        y - (int)((a->centroid_y_q8 * scale + 128u) / 256u), scale);
}

void jelli_canvas_heading(Canvas *c, const char *value, int y, unsigned scale)
{
    int x = 233 - (int)(strlen(value) * 8u * scale) / 2;
    jelli_canvas_text(c, value, x + 2, y + 2, scale, 0u);
    jelli_canvas_text(c, value, x, y, scale, 0xffffu);
}

/* A soft, bounded scrim follows the jelli_canvas_caption, with no scratch buffer. */
void jelli_canvas_caption(Canvas *c, const char *value, int y, uint16_t color)
{
    if (!*value)
        return;
    int half = (int)strlen(value) * 8 + 16;
    for (int dy = -8; dy < 32; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
            int x = 233 + dx, row = y + dy;
            if (x < c->left || row < c->top || x >= c->right || row >= c->bottom ||
                !jelli_canvas_in_round(x, row))
                continue;
            int edge = half - (dx < 0 ? -dx : dx);
            int vertical = dy < 12 ? dy + 8 : 31 - dy;
            unsigned fade = (unsigned)(edge < vertical ? edge : vertical);
            unsigned shade = 16u - (fade > 8u ? 8u : fade);
            uint16_t back = c->s->pixels[(unsigned)row * c->s->stride + (unsigned)x];
            unsigned r = ((back >> 11) & 31u) * shade / 16u;
            unsigned g = ((back >> 5) & 63u) * shade / 16u;
            unsigned b = (back & 31u) * shade / 16u;
            jelli_canvas_pixel(c, x, row, (uint16_t)((r << 11) | (g << 5) | b));
        }
    }
    jelli_canvas_centered(c, value, y, 2u, color);
}

unsigned jelli_canvas_fit_scale(uint32_t id, unsigned target_px)
{
    const JelliAsset *asset = jelli_asset_find(id);
    unsigned scale = asset && asset->width ? target_px / asset->width : 1u;
    return scale ? scale : 1u;
}

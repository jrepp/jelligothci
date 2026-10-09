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
    for (int row = y; row < bottom; ++row)
        for (int column = x; column < right; ++column)
            jelli_canvas_pixel(c, column, row, color);
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
uint16_t jelli_canvas_blend(uint16_t back, uint16_t front, unsigned alpha)
{
    if (!alpha)
        return back;
    if (alpha >= 255u)
        return front;
    unsigned inverse = 255u - alpha;
    unsigned r = (((back >> 11) & 31u) * inverse + ((front >> 11) & 31u) * alpha + 127u) / 255u;
    unsigned g = (((back >> 5) & 63u) * inverse + ((front >> 5) & 63u) * alpha + 127u) / 255u;
    unsigned b = ((back & 31u) * inverse + (front & 31u) * alpha + 127u) / 255u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Composites one already clipped sprite pixel; dimming applies to the source colour,
 * as jelli_canvas_pixel does for opaque drawing. */
static void sprite_pixel(Canvas *c, int x, int y, uint16_t color, unsigned alpha)
{
    if (!jelli_canvas_in_round(x, y))
        return;
    if (c->dim)
        color = (uint16_t)((color >> 1) & 0x7befu);
    uint16_t *destination = &c->s->pixels[(unsigned)y * c->s->stride + (unsigned)x];
    *destination = jelli_canvas_blend(*destination, color, alpha);
}

/* 1:1 and box downscales: one sample per output pixel inside the clip. */
static void sprite_pixels(Canvas *c, const JelliAssetSampler *sampler, int x, int y,
                          const int clip[4])
{
    for (int row = clip[1]; row < clip[3]; ++row) {
        for (int column = clip[0]; column < clip[2]; ++column) {
            uint16_t color = 0u;
            uint8_t alpha =
                jelli_asset_sample(sampler, (unsigned)(column - x), (unsigned)(row - y), &color);
            if (alpha && c->icon_night)
                color = jelli_pet_night_color(color, c->icon_night);
            if (alpha)
                sprite_pixel(c, column, row, color, alpha);
        }
    }
}

/* Nearest upscales: sample each stored pixel once and fill its clipped up x up block,
 * as the original legacy renderer did with jelli_canvas_rect. */
static void sprite_blocks(Canvas *c, const JelliAssetSampler *sampler, int x, int y,
                          const int clip[4])
{
    int up = (int)sampler->up;
    for (int sy = (clip[1] - y) / up; y + sy * up < clip[3]; ++sy) {
        for (int sx = (clip[0] - x) / up; x + sx * up < clip[2]; ++sx) {
            uint16_t color = 0u;
            uint8_t alpha =
                jelli_asset_sample(sampler, (unsigned)(sx * up), (unsigned)(sy * up), &color);
            if (!alpha)
                continue;
            if (c->icon_night)
                color = jelli_pet_night_color(color, c->icon_night);
            int left = x + sx * up, top = y + sy * up;
            int right = left + up < clip[2] ? left + up : clip[2];
            int bottom = top + up < clip[3] ? top + up : clip[3];
            for (int row = top > clip[1] ? top : clip[1]; row < bottom; ++row)
                for (int column = left > clip[0] ? left : clip[0]; column < right; ++column)
                    sprite_pixel(c, column, row, color, alpha);
        }
    }
}

/* Draws at logical `scale` (output = logical size x scale). Legacy art upscales by
 * nearest neighbour exactly as before; high-resolution art is drawn 1:1, upscaled by
 * whole multiples or box-downscaled by whole divisors, then alpha blended. Only the
 * clip rectangle is visited, so partial redraws repeat the full frame's pixels. */
void jelli_canvas_sprite(Canvas *c, uint32_t id, int x, int y, unsigned scale)
{
    JelliAssetSampler sampler;
    if (!jelli_asset_sampler(&sampler, jelli_asset_lookup(c->assets, id), scale))
        return;
    int right = x + (int)sampler.width, bottom = y + (int)sampler.height;
    const int clip[4] = {x > c->left ? x : c->left, y > c->top ? y : c->top,
                         right < c->right ? right : c->right,
                         bottom < c->bottom ? bottom : c->bottom};
    if (clip[0] >= clip[2] || clip[1] >= clip[3])
        return;
    if (sampler.up > 1u)
        sprite_blocks(c, &sampler, x, y, clip);
    else
        sprite_pixels(c, &sampler, x, y, clip);
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

/* A soft, bounded scrim darkens rows y-8..y+31 within half pixels of center_x,
 * with no scratch buffer. Edges fade over eight pixels; the middle is 50 percent. */
void jelli_canvas_scrim(Canvas *c, int center_x, int y, int half)
{
    for (int dy = -8; dy < 32; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
            int x = center_x + dx, row = y + dy;
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
}

void jelli_canvas_caption(Canvas *c, const char *value, int y, uint16_t color)
{
    if (!*value)
        return;
    jelli_canvas_scrim(c, 233, y, (int)strlen(value) * 8 + 16);
    jelli_canvas_centered(c, value, y, 2u, color);
}

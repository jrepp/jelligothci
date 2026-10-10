#include "jelli/agent_scene.h"
#include <stdint.h>
#include <string.h>

/* Positions are 64-bit: rectangle fields are unsigned and draw positions may be
 * negative, and `long` is only 32 bits on the ESP32. */

/* Intersection of [a0, a1) and [b0, b1) on one axis, as a start and a length. */
static void overlap(int64_t a0, int64_t a1, int64_t b0, int64_t b1, unsigned *start,
                    unsigned *length)
{
    int64_t low = a0 > b0 ? a0 : b0, high = a1 < b1 ? a1 : b1;
    *start = high > low ? (unsigned)low : 0u;
    *length = high > low ? (unsigned)(high - low) : 0u;
}

static JelliRect intersect(JelliRect r, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    JelliRect out = {0u, 0u, 0u, 0u};
    overlap(r.x, (int64_t)r.x + r.width, x0, x1, &out.x, &out.width);
    overlap(r.y, (int64_t)r.y + r.height, y0, y1, &out.y, &out.height);
    if (!out.width || !out.height)
        out = (JelliRect){0u, 0u, 0u, 0u};
    return out;
}

JelliRect jelli_agent_rect_intersect(JelliRect a, JelliRect b)
{
    return intersect(a, b.x, b.y, (int64_t)b.x + b.width, (int64_t)b.y + b.height);
}

static JelliRect to_surface(const JelliSurface *s, JelliRect r)
{
    return intersect(r, 0, 0, s->width, s->height);
}

JelliRect jelli_agent_sprite_rect(const JelliSurface *surface, const JelliAsset *asset, int x,
                                  int y, unsigned scale)
{
    JelliAssetSampler sampler;
    JelliRect none = {0u, 0u, 0u, 0u};
    if (!jelli_asset_sampler(&sampler, asset, scale))
        return none;
    JelliRect all = {0u, 0u, surface->width, surface->height};
    return intersect(all, x, y, (int64_t)x + sampler.width, (int64_t)y + sampler.height);
}

JelliRect jelli_agent_rect_union(JelliRect a, JelliRect b)
{
    if (!a.width || !a.height)
        return b;
    if (!b.width || !b.height)
        return a;
    unsigned left = a.x < b.x ? a.x : b.x, top = a.y < b.y ? a.y : b.y;
    unsigned right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
    unsigned bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
    return (JelliRect){left, top, right - left, bottom - top};
}

static uint16_t blend(uint16_t back, uint16_t front, unsigned alpha)
{
    if (alpha >= 255u)
        return front;
    unsigned inverse = 255u - alpha;
    unsigned r = (((back >> 11) & 31u) * inverse + ((front >> 11) & 31u) * alpha + 127u) / 255u;
    unsigned g = (((back >> 5) & 63u) * inverse + ((front >> 5) & 63u) * alpha + 127u) / 255u;
    unsigned b = ((back & 31u) * inverse + (front & 31u) * alpha + 127u) / 255u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void fill(const JelliSurface *s, JelliRect area, uint16_t color, unsigned alpha)
{
    for (unsigned row = area.y; row < area.y + area.height; ++row) {
        uint16_t *line = s->pixels + (size_t)row * s->stride;
        for (unsigned column = area.x; column < area.x + area.width; ++column)
            line[column] = blend(line[column], color, alpha);
    }
}

/* Samples each stored pixel once and fills the up x up block of output pixels it
 * covers inside area (blocks are single pixels when up is 1). */
static void draw_blocks(const JelliSurface *s, const JelliAssetSampler *sampler, int64_t x,
                        int64_t y, JelliRect area)
{
    /* area lies inside the sprite, so every block index below is non-negative. */
    int64_t up = sampler->up;
    int64_t first_row = (area.y - y) / up, last_row = (area.y + (int64_t)area.height - 1 - y) / up;
    int64_t first_column = (area.x - x) / up;
    int64_t last_column = (area.x + (int64_t)area.width - 1 - x) / up;
    for (int64_t by = first_row; by <= last_row; ++by) {
        for (int64_t bx = first_column; bx <= last_column; ++bx) {
            uint16_t color = 0u;
            uint8_t alpha =
                jelli_asset_sample(sampler, (unsigned)(bx * up), (unsigned)(by * up), &color);
            if (alpha)
                fill(
                    s,
                    intersect(area, x + bx * up, y + by * up, x + (bx + 1) * up, y + (by + 1) * up),
                    color, alpha);
        }
    }
}

bool jelli_agent_scene_draw(const JelliSurface *surface, const JelliAsset *asset, int x, int y,
                            unsigned scale, JelliRect clip)
{
    clip = to_surface(surface, clip);
    for (unsigned row = clip.y; row < clip.y + clip.height; ++row)
        memset(surface->pixels + (size_t)row * surface->stride + clip.x, 0,
               clip.width * sizeof(uint16_t));
    JelliAssetSampler sampler;
    if (!jelli_asset_sampler(&sampler, asset, scale))
        return false;
    JelliRect area = intersect(clip, x, y, (int64_t)x + sampler.width, (int64_t)y + sampler.height);
    if (area.width && area.height)
        draw_blocks(surface, &sampler, x, y, area);
    return true;
}

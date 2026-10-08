#include "jelli/engine.h"

static uint16_t rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static void pixel(JelliSurface s, int x, int y, uint16_t color)
{
    if (x >= 0 && y >= 0 && x < (int)s.width && y < (int)s.height)
        /* Writes through a borrowed buffer are observed by the host. */
        // cppcheck-suppress unreadVariable
        s.pixels[(unsigned)y * s.stride + (unsigned)x] = color;
}
static void rectangle(JelliSurface s, int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            pixel(s, xx, yy, color);
}
static void circle(JelliSurface s, int cx, int cy, int radius, uint16_t color)
{
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                pixel(s, cx + x, cy + y, color);
}

static JelliRect damage(const JelliEngine *e, int cx, const uint16_t *colors)
{
    if (!e->rendered)
        return (JelliRect){0, 0, JELLI_WIDTH, JELLI_HEIGHT};
    bool changed = false;
    for (unsigned i = 0; i < JELLI_SHAPE_COUNT; ++i)
        changed = changed || colors[i] != e->last_colors[i];
    if (!changed && cx == e->last_circle_x)
        return (JelliRect){0};
    int left = (cx < e->last_circle_x ? cx : e->last_circle_x) - 34;
    int right = (cx > e->last_circle_x ? cx : e->last_circle_x) + 34;
    int top = 268;
    if (changed) {
        if (left > 112)
            left = 112;
        if (right < 344)
            right = 344;
        top = 126;
    }
    return (JelliRect){(unsigned)left, (unsigned)top, (unsigned)(right - left + 1),
                       (unsigned)(337 - top)};
}

static void clear_damage(JelliSurface s)
{
    const uint16_t background = rgb(16, 24, 36);
    for (unsigned y = s.damage.y; y < s.damage.y + s.damage.height; ++y) {
        int dy = 2 * (int)y - 465;
        for (unsigned x = s.damage.x; x < s.damage.x + s.damage.width; ++x) {
            int dx = 2 * (int)x - 465;
            s.pixels[y * s.stride + x] = dx * dx + dy * dy <= 466 * 466 ? background : 0;
        }
    }
}

void jelli_render(JelliEngine *e)
{
    uint16_t colors[JELLI_SHAPE_COUNT];
    for (unsigned i = 0; i < JELLI_SHAPE_COUNT; ++i)
        colors[i] = jelli_rgb565(e->colors[i].value);
    /* Four-second triangle wave. Speed is independent of rendering rate. */
    int phase = (int)e->animation_ms;
    int travel = phase <= 2000 ? phase : 4000 - phase;
    int cx = 143 + travel * 180 / 2000;
    e->surface.damage = damage(e, cx, colors);
    if (!e->surface.damage.width)
        return;
    JelliSurface s = e->surface;
    clear_damage(s);
    if (s.damage.y < 206u) {
        rectangle(s, 112, 126, 80, 80, colors[0]);
        for (int y = 0; y < 80; ++y)
            rectangle(s, 304 - y / 2, 126 + y, y + 1, 1, colors[1]);
    }
    circle(s, cx, 302, 34, colors[2]);
    for (unsigned i = 0; i < JELLI_SHAPE_COUNT; ++i)
        e->last_colors[i] = colors[i];
    e->last_circle_x = cx;
    e->rendered = true;
}

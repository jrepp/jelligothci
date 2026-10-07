#include "jelli/engine.h"

static uint16_t rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static void pixel(JelliSurface s, int x, int y, uint16_t color)
{
    if (x >= 0 && y >= 0 && x < (int)s.width && y < (int)s.height)
        s.pixels[(unsigned)y*s.stride + (unsigned)x] = color;
}
static void rectangle(JelliSurface s, int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y+h; ++yy)
        for (int xx = x; xx < x+w; ++xx) pixel(s, xx, yy, color);
}
static void circle(JelliSurface s, int cx, int cy, int radius, uint16_t color)
{
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x*x + y*y <= radius*radius) pixel(s, cx+x, cy+y, color);
}

void jelli_render(const JelliEngine *e)
{
    JelliSurface s = e->surface;
    rectangle(s, 0, 0, (int)s.width, (int)s.height, rgb(16,24,36));
    /* Three shapes, drawn entirely in C into the injected surface. */
    rectangle(s, 112, 126, 80, 80, rgb(106,215,255));
    for (int y = 0; y < 80; ++y)
        rectangle(s, 304-y/2, 126+y, y+1, 1, rgb(255,174,99));
    /* Four-second triangle wave. Speed is independent of rendering rate. */
    int phase = (int)e->animation_ms;
    int travel = phase <= 2000 ? phase : 4000-phase;
    circle(s, 143 + travel*180/2000, 302, 34,
           e->paused ? rgb(165,171,185) : rgb(127,235,175));
    /* Match the physical round AMOLED's visible region. */
    for (int y = 0; y < JELLI_HEIGHT; ++y)
        for (int x = 0; x < JELLI_WIDTH; ++x)
            if ((2*x-465)*(2*x-465)+(2*y-465)*(2*y-465) > 466*466)
                pixel(s, x, y, 0);
}

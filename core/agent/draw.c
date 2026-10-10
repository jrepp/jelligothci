#include "jelli/agent_draw.h"
#include "jelli/assets.h"

uint16_t jelli_agent_rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | ((b & 0xffu) >> 3));
}

/* The span [start, start + length) clipped to [0, limit), in 64 bits so no sum of
 * caller values can overflow; false when nothing is left. */
static bool clip(int start, int length, unsigned limit, unsigned *from, unsigned *to)
{
    int64_t first = start < 0 ? 0 : start, last = (int64_t)start + length;
    if (last > (int64_t)limit)
        last = (int64_t)limit;
    if (length <= 0 || first >= last)
        return false;
    *from = (unsigned)first;
    *to = (unsigned)last;
    return true;
}

void jelli_agent_fill(JelliSurface *surface, int x, int y, int width, int height, uint16_t color)
{
    unsigned x0, x1, y0, y1;
    if (!surface || !surface->pixels || !clip(x, width, surface->width, &x0, &x1) ||
        !clip(y, height, surface->height, &y0, &y1))
        return;
    for (unsigned row = y0; row < y1; ++row) {
        uint16_t *line = surface->pixels + (size_t)row * surface->stride;
        for (unsigned column = x0; column < x1; ++column)
            line[column] = color;
    }
}

/* The largest whole number whose square is at most value. */
static int root(int value)
{
    int result = 0;
    while ((result + 1) * (result + 1) <= value)
        ++result;
    return result;
}

/* How far row (0 at the top) of a rounded rectangle starts in from its sides. */
static int inset(int row, int height, int radius)
{
    int from_edge = row < height - 1 - row ? row : height - 1 - row;
    if (from_edge >= radius)
        return 0;
    int d = radius - from_edge;
    return radius - root(radius * radius - (d - 1) * (d - 1));
}

static int bounded_radius(int width, int height, int radius)
{
    int shorter = width < height ? width : height;
    if (radius > shorter / 2)
        radius = shorter / 2;
    return radius < 0 ? 0 : radius;
}

void jelli_agent_round_rect(JelliSurface *surface, int x, int y, int width, int height, int radius,
                            uint16_t color)
{
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return;
    radius = bounded_radius(width, height, radius);
    for (int row = 0; row < height; ++row) {
        int in = inset(row, height, radius);
        jelli_agent_fill(surface, x + in, y + row, width - 2 * in, 1, color);
    }
}

void jelli_agent_round_outline(JelliSurface *surface, int x, int y, int width, int height,
                               int radius, int thickness, uint16_t color)
{
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || thickness <= 0)
        return;
    radius = bounded_radius(width, height, radius);
    int inner = radius > thickness ? radius - thickness : 0;
    for (int row = 0; row < height; ++row) {
        int out = inset(row, height, radius);
        bool band = row < thickness || row >= height - thickness;
        int half = (width + 1) / 2;
        int in = band ? half : thickness + inset(row - thickness, height - 2 * thickness, inner);
        if (in > half)
            in = half;
        jelli_agent_fill(surface, x + out, y + row, in - out, 1, color);
        jelli_agent_fill(surface, x + width - in, y + row, in - out, 1, color);
    }
}

static unsigned bounded_scale(unsigned scale)
{
    if (!scale)
        return 1u;
    return scale > JELLI_AGENT_TEXT_MAX_SCALE ? JELLI_AGENT_TEXT_MAX_SCALE : scale;
}

static size_t bounded_length(size_t length)
{
    return length > JELLI_AGENT_TEXT_MAX_LENGTH ? JELLI_AGENT_TEXT_MAX_LENGTH : length;
}

int jelli_agent_text_width(size_t length, unsigned scale)
{
    return (int)(bounded_length(length) * JELLI_AGENT_GLYPH_WIDTH * bounded_scale(scale));
}

static void glyph(JelliSurface *surface, int x, int y, uint8_t byte, int scale, uint16_t color)
{
    uint8_t code = byte >= 32u && byte <= 126u ? byte : (uint8_t)'?';
    const uint8_t *rows = jelli_agent_lowercase(code);
    if (!rows)
        rows = jelli_asset_lookup_glyph(NULL, code);
    if (!rows)
        return;
    for (int row = 0; row < JELLI_AGENT_GLYPH_HEIGHT; ++row)
        for (int column = 0; column < JELLI_AGENT_GLYPH_WIDTH; ++column)
            if (rows[row] & (uint8_t)(1u << (7u - (unsigned)column)))
                jelli_agent_fill(surface, x + column * scale, y + row * scale, scale, scale, color);
}

void jelli_agent_text(JelliSurface *surface, int x, int y, const char *text, size_t length,
                      unsigned scale, uint16_t color)
{
    if (!text)
        return;
    int step = (int)(JELLI_AGENT_GLYPH_WIDTH * bounded_scale(scale));
    length = bounded_length(length);
    for (size_t i = 0u; i < length; ++i)
        glyph(surface, x + (int)i * step, y, (uint8_t)text[i], (int)bounded_scale(scale), color);
}

void jelli_agent_text_centered(JelliSurface *surface, int cx, int y, const char *text,
                               size_t length, unsigned scale, uint16_t color)
{
    jelli_agent_text(surface, cx - jelli_agent_text_width(length, scale) / 2, y, text, length,
                     scale, color);
}

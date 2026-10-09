#include "jelli/agent_present.h"
#include <stdint.h>

/* [start, start + length) clipped to [0, limit) and widened to even edges; limit is
 * even, so the rounded-up end never passes it. Empty spans come back as 0..0. */
static void even_span(unsigned start, unsigned length, unsigned limit, unsigned *low,
                      unsigned *high)
{
    uint64_t end = (uint64_t)start + length;
    if (end > limit)
        end = limit;
    if (start >= end) {
        *low = *high = 0u;
        return;
    }
    *low = start & ~1u;
    *high = (unsigned)((end + 1u) & ~(uint64_t)1u);
}

bool jelli_agent_present_window(JelliRect rect, unsigned width, unsigned height,
                                size_t stripe_pixels, JelliAgentWindow *window)
{
    if (!width || !height || ((width | height) & 1u))
        return false;
    unsigned x0, x1, y0, y1;
    even_span(rect.x, rect.width, width, &x0, &x1);
    even_span(rect.y, rect.height, height, &y0, &y1);
    if (x0 >= x1 || y0 >= y1)
        return false;
    size_t rows = (stripe_pixels / (x1 - x0)) & ~(size_t)1u;
    if (!rows)
        return false;
    if (rows > y1 - y0)
        rows = y1 - y0; /* Even too: the window's height is even. */
    *window = (JelliAgentWindow){x0, y0, x1, y1, (unsigned)rows};
    return true;
}

unsigned jelli_agent_window_rows(const JelliAgentWindow *window, unsigned y)
{
    if (y >= window->y1)
        return 0u;
    unsigned left = window->y1 - y;
    return left < window->rows_per_send ? left : window->rows_per_send;
}

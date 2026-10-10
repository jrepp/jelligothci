#ifndef JELLI_DISPLAY_COPY_H
#define JELLI_DISPLAY_COPY_H

#include "jelli/engine.h"
#include <string.h>

/* Caller validates damage bounds and owns non-overlapping 466x466 buffers.
 * Both buffers have a packed JELLI_WIDTH stride. No asynchronous reads. */
static inline void jelli_display_copy(uint16_t *destination, const uint16_t *source, JelliRect r)
{
    if (!r.width || !r.height)
        return;
    if (r.width == JELLI_WIDTH) {
        size_t offset = (size_t)r.y * JELLI_WIDTH;
        memcpy(destination + offset, source + offset,
               (size_t)r.height * JELLI_WIDTH * sizeof(*source));
        return;
    }
    for (unsigned y = r.y; y < r.y + r.height; ++y) {
        size_t offset = (size_t)y * JELLI_WIDTH + r.x;
        memcpy(destination + offset, source + offset, r.width * sizeof(*source));
    }
}

#endif

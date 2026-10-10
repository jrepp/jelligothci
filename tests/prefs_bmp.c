#include "prefs_bmp.h"
#include <stdio.h>
#include <string.h>

enum { MAX_SIDE = 466 };

/* Bytes of a row: three per pixel, padded to a multiple of four. */
static size_t row_bytes(unsigned side) { return ((size_t)side * 3u + 3u) / 4u * 4u; }

static bool write_header(FILE *file, unsigned side)
{
    uint8_t header[54] = {'B', 'M'};
    const uint32_t data = side * (uint32_t)row_bytes(side),
                   values[] = {54u + data, 0u, 54u, 40u, side, side};
    memcpy(header + 2, &values[0], 4u);
    memcpy(header + 10, &values[2], 16u); /* Offset, header size, width, height. */
    header[26] = 1u;
    header[28] = 24u;
    return fwrite(header, 1u, sizeof(header), file) == sizeof(header);
}

bool prefs_bmp_save(const char *folder, const char *name, const uint16_t *pixels, unsigned side,
                    unsigned stride)
{
    char path[512];
    if (!folder || !name || !pixels || side == 0u || side > MAX_SIDE || stride < side)
        return false;
    int length = snprintf(path, sizeof(path), "%s/%s.bmp", folder, name);
    FILE *file = length > 0 && (size_t)length < sizeof(path) ? fopen(path, "wb") : NULL;
    if (!file)
        return false;
    bool ok = write_header(file, side);
    int64_t center = (int64_t)side / 2, radius = center;
    for (int y = (int)side - 1; ok && y >= 0; --y) {
        uint8_t row[MAX_SIDE * 3 + 4] = {0};
        for (unsigned x = 0u; x < side; ++x) {
            uint16_t pixel = pixels[(size_t)y * stride + x];
            int64_t dx = (int64_t)x - center, dy = (int64_t)y - center;
            bool outside = dx * dx + dy * dy > radius * radius;
            row[x * 3u + 0u] = outside ? 90u : (uint8_t)((pixel & 31u) << 3);
            row[x * 3u + 1u] = outside ? 90u : (uint8_t)(((pixel >> 5) & 63u) << 2);
            row[x * 3u + 2u] = outside ? 90u : (uint8_t)((pixel >> 11) << 3);
        }
        ok = fwrite(row, 1u, row_bytes(side), file) == row_bytes(side);
    }
    return fclose(file) == 0 && ok;
}

#include "hires_fixture.h"

/* The production densities from assets/slice/README.md, by asset ID range. */
static const struct {
    uint32_t first, last;
    unsigned density;
} targets[] = {{1001u, 1048u, 6u}, {2001u, 2012u, 6u}, {6001u, 6013u, 3u},   {7001u, 7005u, 2u},
               {8001u, 8009u, 3u}, {9001u, 9008u, 6u}, {11001u, 11009u, 2u}, {12001u, 12007u, 4u}};

unsigned hires_fixture_density(uint32_t id)
{
    for (size_t i = 0u; i < sizeof(targets) / sizeof(targets[0]); ++i)
        if (id >= targets[i].first && id <= targets[i].last)
            return targets[i].density;
    return 0u;
}

static bool covered(const JelliAsset *legacy, unsigned density, int x, int y)
{
    if (x < 0 || y < 0)
        return false;
    return jelli_asset_alpha_at(legacy, (unsigned)x / density, (unsigned)y / density) != 0u;
}

/* Colour of the first covered 4-neighbour, for the soft outer edge. */
static uint16_t neighbour_color(const JelliAsset *legacy, unsigned density, int x, int y)
{
    static const int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (unsigned i = 0u; i < 4u; ++i) {
        int nx = x + offsets[i][0], ny = y + offsets[i][1];
        if (covered(legacy, density, nx, ny))
            return legacy->pixels[(unsigned)ny / density * legacy->width + (unsigned)nx / density];
    }
    return 0u;
}

static unsigned covered_neighbours(const JelliAsset *legacy, unsigned density, int x, int y)
{
    return (unsigned)covered(legacy, density, x - 1, y) +
           (unsigned)covered(legacy, density, x + 1, y) +
           (unsigned)covered(legacy, density, x, y - 1) +
           (unsigned)covered(legacy, density, x, y + 1);
}

bool hires_fixture_upscale(JelliAsset *out, HiresPool *pool, const JelliAsset *legacy,
                           unsigned density)
{
    if (!out || !pool || !legacy || !legacy->pixels || !legacy->mask || density < 1u ||
        density > JELLI_ASSET_MAX_DENSITY || jelli_asset_density(legacy) != 1u)
        return false;
    unsigned width = legacy->width * density, height = legacy->height * density;
    size_t count = (size_t)width * height;
    if (pool->used > pool->capacity || count > pool->capacity - pool->used)
        return false;
    uint16_t *pixels = pool->pixels + pool->used;
    uint8_t *alpha = pool->alpha + pool->used;
    for (unsigned y = 0u; y < height; ++y) {
        for (unsigned x = 0u; x < width; ++x) {
            size_t at = (size_t)y * width + x;
            unsigned around = covered_neighbours(legacy, density, (int)x, (int)y);
            if (covered(legacy, density, (int)x, (int)y)) {
                pixels[at] = legacy->pixels[y / density * legacy->width + x / density];
                alpha[at] = around == 4u ? 255u : 192u;
            } else {
                pixels[at] = around ? neighbour_color(legacy, density, (int)x, (int)y) : 0u;
                alpha[at] = around ? 64u : 0u;
            }
        }
    }
    pool->used += count;
    *out = *legacy;
    out->pixels = pixels;
    out->mask = NULL;
    out->mask_stride = 0u;
    out->alpha = alpha;
    out->density = (uint8_t)density;
    return true;
}

size_t hires_fixture_set(JelliAsset *items, size_t capacity, HiresPool *pool)
{
    size_t count = 0u;
    for (size_t i = 0u; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        for (uint32_t id = targets[i].first; id <= targets[i].last; ++id) {
            const JelliAsset *embedded = jelli_asset_find(id);
            if (!embedded)
                continue;
            if (count == capacity)
                return 0u;
            /* Once real high-resolution art is embedded, test against it directly. */
            if (embedded->alpha && jelli_asset_density(embedded) == targets[i].density)
                items[count] = *embedded;
            else if (!hires_fixture_upscale(&items[count], pool, embedded, targets[i].density))
                return 0u;
            ++count;
        }
    }
    return count;
}

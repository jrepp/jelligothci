#ifndef JELLI_TEST_HIRES_FIXTURE_H
#define JELLI_TEST_HIRES_FIXTURE_H

#include "jelli/assets.h"

/* Caller-owned storage for generated high-resolution test art. */
typedef struct {
    uint16_t *pixels;
    uint8_t *alpha;
    size_t capacity, used; /* Stored pixels. */
} HiresPool;

/* Target density of an asset ID in the high-resolution set (0 = stays legacy). */
unsigned hires_fixture_density(uint32_t id);

/* Nearest-upscales a legacy asset by `density` into `pool` with soft edges: covered
 * pixels on the coverage boundary get alpha 192 and uncovered pixels touching it get
 * 64 in the neighbour's colour. Coverage (alpha >= 128) is the exact upscale, so the
 * logical bounds and anchors equal the legacy asset's; they are copied unchanged. */
bool hires_fixture_upscale(JelliAsset *out, HiresPool *pool, const JelliAsset *legacy,
                           unsigned density);

/* Fills `items` with a high-resolution version of every embedded asset that has a
 * target density: embedded art already at that density is copied as is, legacy art is
 * upscaled into the pool. Returns the number written, or 0 when storage runs out. */
size_t hires_fixture_set(JelliAsset *items, size_t capacity, HiresPool *pool);

#endif

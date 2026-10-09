#ifndef JELLI_TEST_ASSET_REFERENCE_H
#define JELLI_TEST_ASSET_REFERENCE_H

#include "jelli/assets.h"

/* An independent model of the density sampler and compositor (include/jelli/assets.h,
 * core/pet_canvas.c), written from the documented contract rather than the code, plus a
 * seeded generator of synthetic sprites. Property tests compare production output with
 * it pixel for pixel. */

/* Seeded xorshift32; tests print the seed of a failing case so it replays. */
typedef struct {
    uint32_t state;
} RefRandom;
uint32_t ref_next(RefRandom *random);
unsigned ref_below(RefRandom *random, unsigned bound);

/* A synthetic sprite whose planes are separate heap blocks of exactly their stored
 * size: any read past a plane trips AddressSanitizer (make sanitize). */
typedef struct {
    JelliAsset asset;
    uint16_t *pixels;
    uint8_t *alpha, *mask;
    size_t count; /* Stored pixels in the pixel plane. */
} RefSprite;

/* Random logical size (1..max_logical), density field 0..9 or 255, an alpha or a
 * 1-bit mask plane with padded rows, and one of several alpha patterns (clear, opaque,
 * one mid value, uniform, mostly binary). With `broken`, about one sprite in ten is
 * also an incomplete descriptor (no pixels, zero size, no plane, short mask rows). */
void ref_sprite_random(RefSprite *sprite, RefRandom *random, uint32_t id, unsigned max_logical,
                       bool broken);
/* Allocates an alpha-plane sprite with zeroed planes; the caller fills them. */
void ref_sprite_alpha(RefSprite *sprite, uint32_t id, unsigned width, unsigned height,
                      unsigned density);
/* Fills the alpha plane with one of the generator's patterns. */
void ref_fill_alpha(uint8_t *alpha, size_t count, RefRandom *random);
void ref_sprite_free(RefSprite *sprite);

/* Effective density: 0 and 1 are legacy 1, 2..8 as stored, anything else unusable. */
unsigned ref_density(const JelliAsset *asset);
/* Planes present and the mask (when used) at least as wide as a stored row. */
bool ref_readable(const JelliAsset *asset);
/* The scale rule alone: 1..16 and a multiple or divisor of the density. */
bool ref_supported(const JelliAsset *asset, unsigned scale);
/* Stored coverage at a stored pixel; the caller keeps (x, y) inside the image. */
uint8_t ref_texel(const JelliAsset *asset, unsigned x, unsigned y);
/* Straight-alpha sample of output pixel (x, y) at `scale` (drawable sprites only):
 * the n x n stored block under it (n = density / scale when downscaling, else 1),
 * alpha the rounded mean, colour the alpha-weighted rounded mean per channel. */
unsigned ref_sample(const JelliAsset *asset, unsigned scale, unsigned x, unsigned y,
                    uint16_t *color);
/* Per-channel rounding straight-alpha blend of RGB565 colours. */
uint16_t ref_blend(uint16_t back, uint16_t front, unsigned alpha);
/* The cached logical bounds equal the stored coverage (alpha >= 128) bounds divided
 * by the density: floor for left/top, ceiling for right/bottom; false when empty. */
bool ref_bounds_match(const JelliAsset *asset);
/* The 466-pixel round panel: pixel centres within radius 233 of (232.5, 232.5). */
bool ref_in_round(int x, int y);

#endif

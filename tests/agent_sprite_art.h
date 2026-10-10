#ifndef JELLI_TEST_AGENT_SPRITE_ART_H
#define JELLI_TEST_AGENT_SPRITE_ART_H

#include "jelli/agent_sprite.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Seeded generators for the agent sprite's property tests (tests/test_agent_sprite_*).
 * Every plane is its own heap block of exactly its size, so the sanitizer build catches
 * any read or write past one. */

#define CHECK_CASE(expr, seed)                                                                     \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed %u)\n", __FILE__, __LINE__, #expr, (unsigned)(seed)); \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* xorshift32; a zero state is replaced so it never sticks at zero. */
uint32_t art_next(uint32_t *state);
unsigned art_below(uint32_t *state, unsigned bound);
void *art_calloc(size_t count, size_t size);

/* How the alpha plane is filled. Clear texels always hold garbage colour. */
typedef enum { ART_SPARSE, ART_OPAQUE, ART_NOISE, ART_BINARY, ART_EMPTY, ART_KINDS } ArtCoverage;

/* A generated asset that owns its planes; width and height are stored pixels. */
typedef struct {
    JelliAsset asset;
    uint16_t *pixels;
    uint8_t *alpha, *mask;
    unsigned width, height;
} Art;

/* Logical width x height at density (0 is legacy 1). With mask, coverage is a 1-bit
 * MSB-first mask plane instead of an alpha plane. */
Art art_make(uint32_t *state, unsigned width, unsigned height, unsigned density, bool mask,
             ArtCoverage kind);
/* Random density (0, 1, 2, 3 or 8), mask or alpha, size and coverage. */
Art art_random(uint32_t *state);
void art_free(Art *art);
/* Stored coverage, 0 outside the image. */
uint8_t art_alpha(const Art *art, long x, long y);
void art_set_alpha(Art *art, unsigned x, unsigned y, uint8_t alpha);

/* A random non-empty source inside the stored image, edges included. */
JelliRect art_source(uint32_t *state, const Art *art);
/* A random output length for a source length: 1, integer stretches, odd ratios. */
unsigned art_size(uint32_t *state, unsigned source);

/* Caller-owned sprite planes, exactly width x height. */
typedef struct {
    uint16_t *pixels;
    uint8_t *alpha;
    JelliAgentSprite sprite;
} Planes;

Planes planes_make(unsigned width, unsigned height);
void planes_free(Planes *planes);
size_t planes_count(const Planes *planes);

#endif

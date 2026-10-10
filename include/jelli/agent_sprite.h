#ifndef JELLI_AGENT_SPRITE_H
#define JELLI_AGENT_SPRITE_H

#include "jelli/assets.h"
#include "jelli/engine.h"
#include <stdbool.h>
#include <stdint.h>

/* The voice agent's large character (RFC-003) at any size: a part of a stored sprite
 * is resampled once into caller-owned planes, then drawn every frame by copying, so
 * the unicorn can fill the round panel at a size the whole-number sampler cannot
 * reach. Portable and allocation-free; the host owns every buffer. */

/* Straight RGB565 colour and 8-bit alpha, width x height, rows packed. */
typedef struct {
    uint16_t *pixels;
    uint8_t *alpha;
    unsigned width, height;
} JelliAgentSprite;

/* The stored pixels of asset with any coverage (alpha > 0): its content, to crop to.
 * Empty (zero width) for an unreadable or empty asset. */
JelliRect jelli_agent_asset_content(const JelliAsset *asset);

/* How far the opaque coverage (alpha >= 128) reaches from the centre of area, in
 * stored pixels: the radius of the smallest circle about that centre that holds it.
 * Zero for an unreadable asset. */
float jelli_agent_asset_reach(const JelliAsset *asset, JelliRect area);

/* Fills sprite (its planes sized width x height by the caller) from the stored area
 * source of asset, stretched to the sprite's size with bilinear filtering of
 * premultiplied colour, so transparent texels never tint an edge. False, leaving the
 * planes untouched, for an unreadable asset, an empty sprite, or a source that is
 * empty or reaches outside the stored image. */
bool jelli_agent_sprite_resample(JelliAgentSprite *sprite, const JelliAsset *asset,
                                 JelliRect source);

/* The rectangle a sprite covers at (x, y), clipped to the surface. */
JelliRect jelli_agent_sprite_area(const JelliSurface *surface, const JelliAgentSprite *sprite,
                                  int x, int y);

/* Clears clip (intersected with the surface) to black, then alpha-blends the part of
 * the sprite at (x, y) that falls inside it, flipped left to right when mirror is
 * set. Drawing a frame as several clips gives the same pixels as drawing it whole. */
void jelli_agent_sprite_draw(const JelliSurface *surface, const JelliAgentSprite *sprite, int x,
                             int y, bool mirror, JelliRect clip);

#endif

#ifndef JELLI_AGENT_SCENE_H
#define JELLI_AGENT_SCENE_H

#include "jelli/assets.h"
#include "jelli/engine.h"
#include <stdbool.h>

/* The voice agent's large character (RFC-003): a high-resolution sprite drawn at a
 * whole logical scale over black, redrawn only inside a clip rectangle so animation
 * can update small regions. Portable and allocation-free; the host owns the surface
 * and presents whatever the caller treats as damage. */

/* The rectangle a sprite covers at (x, y) and scale, clipped to the surface; empty
 * (zero width) when it is off-screen or the asset cannot be drawn at that scale. */
JelliRect jelli_agent_sprite_rect(const JelliSurface *surface, const JelliAsset *asset, int x,
                                  int y, unsigned scale);

/* The smallest rectangle covering both; an empty rectangle adds nothing. */
JelliRect jelli_agent_rect_union(JelliRect a, JelliRect b);

/* The overlap of both; empty (all zero) when they do not overlap. */
JelliRect jelli_agent_rect_intersect(JelliRect a, JelliRect b);

/* Clears clip (intersected with the surface) to black, then alpha-blends the part of
 * the sprite at (x, y) and scale that falls inside it. Drawing a frame as several
 * clips gives the same pixels as drawing it whole. Returns false when the asset
 * cannot be drawn at that scale; the clip is cleared either way. */
bool jelli_agent_scene_draw(const JelliSurface *surface, const JelliAsset *asset, int x, int y,
                            unsigned scale, JelliRect clip);

#endif

#ifndef JELLI_AGENT_DRAW_H
#define JELLI_AGENT_DRAW_H

#include "jelli/engine.h"
#include <stddef.h>
#include <stdint.h>

/* Flat drawing for Loki Agent's own screens (ADR-016): filled rectangles, rounded
 * buttons, and ASCII text in the pets' 8x12 pixel font scaled by whole pixels. All of
 * it is clipped to the surface, so any coordinates are safe. Bytes outside 32..126
 * draw as '?', so text from the air (Wi-Fi names) never indexes past the font.
 * Colours are native RGB565. Portable and allocation-free. */

enum {
    JELLI_AGENT_GLYPH_WIDTH = 8,
    JELLI_AGENT_GLYPH_HEIGHT = 12,
    JELLI_AGENT_TEXT_MAX_SCALE = 8,  /* Larger scales draw at this one. */
    JELLI_AGENT_TEXT_MAX_LENGTH = 64 /* Longer text draws its first 64 bytes. */
};

/* The rows of a lowercase letter's glyph (a..z), else NULL; the pets' font has
 * capitals only. */
const uint8_t *jelli_agent_lowercase(uint8_t letter);

/* Native RGB565 from 8-bit channels. */
uint16_t jelli_agent_rgb(unsigned r, unsigned g, unsigned b);

void jelli_agent_fill(JelliSurface *surface, int x, int y, int width, int height, uint16_t color);

/* A rectangle with its corners rounded by radius, at most half its shorter side. */
void jelli_agent_round_rect(JelliSurface *surface, int x, int y, int width, int height, int radius,
                            uint16_t color);

/* A ring of the given thickness inside a rounded rectangle's edge. */
void jelli_agent_round_outline(JelliSurface *surface, int x, int y, int width, int height,
                               int radius, int thickness, uint16_t color);

/* Width in pixels of length glyphs (at most JELLI_AGENT_TEXT_MAX_LENGTH) at scale. */
int jelli_agent_text_width(size_t length, unsigned scale);

/* Text with its top-left corner at (x, y). */
void jelli_agent_text(JelliSurface *surface, int x, int y, const char *text, size_t length,
                      unsigned scale, uint16_t color);

/* Text centred horizontally on cx. */
void jelli_agent_text_centered(JelliSurface *surface, int cx, int y, const char *text,
                               size_t length, unsigned scale, uint16_t color);

#endif

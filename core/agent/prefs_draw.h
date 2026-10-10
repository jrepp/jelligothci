#ifndef JELLI_AGENT_PREFS_DRAW_H
#define JELLI_AGENT_PREFS_DRAW_H

#include "jelli/agent_draw.h"
#include "prefs_internal.h"

/* Settings' palette and the parts its screens share (prefs_parts.c). Native RGB565:
 * lilac is the agent's colour (its identity ring), warm marks a failure. */
#define PREFS_BLACK ((uint16_t)0x0000u)
#define PREFS_WHITE ((uint16_t)0xffffu)
#define PREFS_LILAC ((uint16_t)0xcd1fu)   /* 200, 162, 255 */
#define PREFS_GREY ((uint16_t)0x8c72u)    /* 140, 140, 150 */
#define PREFS_KEY ((uint16_t)0x2967u)     /* 44, 44, 56: a key or a row */
#define PREFS_KEY_DIM ((uint16_t)0x18e4u) /* 30, 30, 38: a key that does nothing yet */
#define PREFS_WARM ((uint16_t)0xfc6eu)    /* 255, 140, 112 */

typedef enum {
    PREFS_PRIMARY,   /* Lilac, black words: the way forward. */
    PREFS_SECONDARY, /* A lilac outline, white words. */
    PREFS_DISABLED   /* Dim, grey words: not yet. */
} PrefsButtonStyle;

void prefs_button(JelliSurface *surface, PrefsBox box, const char *label, PrefsButtonStyle style);

/* A network's name, shortened to at most limit glyphs with "..", at scale 2. */
void prefs_name(JelliSurface *surface, int x, int y, const uint8_t *ssid, size_t size, size_t limit,
                uint16_t color);
void prefs_name_centered(JelliSurface *surface, int y, const uint8_t *ssid, size_t size,
                         size_t limit, uint16_t color);

/* One listed network: its name, a lock when it needs a password, its signal as bars,
 * and an outline when the board uses it. */
void prefs_network_row(JelliSurface *surface, PrefsBox box, const JelliPrefsNetwork *network,
                       bool current);

void prefs_keyboard(JelliSurface *surface, const JelliPrefs *prefs);

#endif

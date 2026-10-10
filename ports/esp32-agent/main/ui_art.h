#ifndef JELLI_AGENT_UI_ART_H
#define JELLI_AGENT_UI_ART_H

#include "jelli/agent_prance.h"
#include "jelli/agent_sprite.h"
#include <stdbool.h>

/* Internal to the UI (ui.c): the unicorn's frames, resampled once into PSRAM. */

enum { JELLI_UI_SIDE = 466 };

typedef struct {
    /* The two idle poses at full size, centred on the panel. */
    JelliAgentSprite home[2];
    /* The prancing frames, all cut from one crop of their 192-pixel frames so they line
     * up, at JELLI_AGENT_PRANCE_FRAME_PX per frame. Within that frame a sprite sits at
     * (x, y), or (mirror_x, y) when flipped. */
    JelliAgentSprite prance[JELLI_PRANCE_FRAMES];
    int x, mirror_x, y;
} JelliUiArt;

/* Fills art from the embedded unicorn frames; false (and nothing kept) when an asset
 * or PSRAM is missing or a size is out of range. */
bool jelli_agent_ui_art_prepare(JelliUiArt *art);

#endif

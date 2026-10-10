#ifndef JELLI_PET_CANVAS_H
#define JELLI_PET_CANVAS_H
#include "jelli/pet_ui.h"
#define BG 0x18e3u
#define INK 0x4989u
#define PALE 0xff99u
#define MINT 0x8736u
#define TEAL 0x1bcfu
#define GOLD 0xf62cu
#define PINK 0xfc73u

typedef struct {
    JelliSurface *s;
    int left, top, right, bottom;
    uint8_t icon_night;
    bool dim;
    const JelliAssetSet *assets;
} Canvas;

bool jelli_canvas_in_round(int x, int y);
void jelli_canvas_pixel(Canvas *c, int x, int y, uint16_t color);
void jelli_canvas_rect(Canvas *c, int x, int y, int w, int h, uint16_t color);
void jelli_canvas_disk(Canvas *c, int x, int y, int radius, uint16_t color);
void jelli_canvas_text(Canvas *c, const char *value, int x, int y, unsigned scale, uint16_t color);
void jelli_canvas_centered(Canvas *c, const char *value, int y, unsigned scale, uint16_t color);
void jelli_canvas_sprite(Canvas *c, uint32_t id, int x, int y, unsigned scale);
void jelli_canvas_centered_sprite(Canvas *c, uint32_t id, int x, int y, unsigned scale);
/* Largest whole scale that keeps sprite id's width within target_px (at least 1). Uses the
 * asset's real width, never its ID range; live packs cannot change dimensions. */
unsigned jelli_canvas_fit_scale(uint32_t id, unsigned target_px);
void jelli_canvas_heading(Canvas *c, const char *value, int y, unsigned scale);
void jelli_canvas_caption(Canvas *c, const char *value, int y, uint16_t color);
#endif

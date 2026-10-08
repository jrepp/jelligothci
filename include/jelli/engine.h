#ifndef JELLI_ENGINE_H
#define JELLI_ENGINE_H

#include "jelli/animation.h"
#include <stdbool.h>
#include <stdint.h>

#define JELLI_WIDTH 466
#define JELLI_HEIGHT 466
#define JELLI_SHAPE_COUNT 3
#define JELLI_COLOR_DURATION_MS 300u

/* Damage is bounded to the surface; zero width/height means no changed pixels. */
typedef struct {
    unsigned x, y, width, height;
} JelliRect;

/* Host-owned native-endian RGB565 memory. stride is measured in pixels.
 * The renderer requires a 466x466 drawable surface. Preserve pixels between
 * frames; the first frame initializes everything, later frames update damage. */
typedef struct {
    uint16_t *pixels;
    unsigned width, height, stride;
    JelliRect damage;
} JelliSurface;

typedef enum { JELLI_TAP, JELLI_TOGGLE_PAUSE, JELLI_QUIT } JelliInputKind;
typedef struct {
    JelliInputKind kind;
    int x, y;
} JelliInput;

/* All callbacks execute on the engine's calling thread.
 * now_ms: injected monotonic milliseconds (real or simulated).
 * poll: false when drained; tap coordinates are surface pixels.
 * present: finish reading/copying the surface before returning.
 * paused: optional output notification, e.g. host UI or logging.
 * now_ms and present are required; poll and paused may be NULL.
 * ctx and the surface memory must outlive the engine. */
typedef struct {
    void *ctx;
    uint64_t (*now_ms)(void *ctx);
    bool (*poll)(void *ctx, JelliInput *input);
    void (*present)(void *ctx, const JelliSurface *surface);
    void (*paused)(void *ctx, bool paused);
} JelliPlatform;

typedef struct {
    JelliPlatform platform;
    JelliSurface surface;
    uint64_t last_ms;
    uint32_t animation_ms;
    JelliColorTween colors[JELLI_SHAPE_COUNT];
    unsigned palette;
    int last_circle_x;
    uint16_t last_colors[JELLI_SHAPE_COUNT];
    bool rendered;
    bool running, paused;
} JelliEngine;

bool jelli_init(JelliEngine *engine, JelliPlatform platform, JelliSurface surface);
bool jelli_frame(JelliEngine *engine);
void jelli_render(JelliEngine *engine);

#endif

#ifndef JELLI_ENGINE_H
#define JELLI_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#define JELLI_WIDTH 466
#define JELLI_HEIGHT 466

/* Host-owned native-endian RGB565 memory. stride is measured in pixels.
 * The initial renderer requires a 466x466 drawable surface. */
typedef struct {
    uint16_t *pixels;
    unsigned width, height, stride;
} JelliSurface;

typedef enum { JELLI_TAP, JELLI_TOGGLE_PAUSE, JELLI_QUIT } JelliInputKind;
typedef struct { JelliInputKind kind; int x, y; } JelliInput;

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
    bool running, paused;
} JelliEngine;

bool jelli_init(JelliEngine *engine, JelliPlatform platform, JelliSurface surface);
bool jelli_frame(JelliEngine *engine);
void jelli_render(const JelliEngine *engine);

#endif

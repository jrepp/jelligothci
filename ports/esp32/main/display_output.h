#ifndef JELLI_DISPLAY_OUTPUT_H
#define JELLI_DISPLAY_OUTPUT_H
#include "jelli/debug.h"
#include "lvgl.h"
#include "display_transfer.h"

typedef struct {
    uint16_t *engine_pixels, *canvas_pixels;
    lv_obj_t *canvas;
    JelliDisplayTransfer transfer;
    uint32_t submitted, failed;
    uint64_t checked_ms;
    uint32_t checks, mismatches;
    unsigned stack_free;
    bool guards_ok, heap_ok, buffers_equal, refresh_requested;
} JelliDisplayOutput;

/* Startup allocations: two 434,312-byte PSRAM frames plus 128 bytes of guards.
 * Engine task owns state; canvas access also holds the BSP display mutex. */
void jelli_display_output_init(JelliDisplayOutput *output);
void jelli_display_output_present(JelliDisplayOutput *output, const JelliSurface *surface,
                                  uint64_t now);
bool jelli_display_output_command(void *ctx, JelliDebug *debug, const JelliPetEngine *engine,
                                  uint32_t id, char **words, unsigned count);
#endif

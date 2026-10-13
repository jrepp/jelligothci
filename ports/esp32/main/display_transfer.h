#ifndef JELLI_DISPLAY_TRANSFER_H
#define JELLI_DISPLAY_TRANSFER_H
#include "lvgl.h"
#include "jelli/engine.h"
#include "esp_lcd_panel_ops.h"
#include <stdint.h>

enum { JELLI_TRANSFER_BYTES = JELLI_WIDTH * 50 * sizeof(uint16_t) };

typedef struct {
    void *pixels;
    esp_lcd_panel_handle_t panel;
    uint32_t submitted, failed;
} JelliDisplayTransfer;

/* Reserve a single internal DMA strip before starting the BSP and Wi-Fi.
 * The LVGL task owns it until the panel completion callback releases a flush. */
void jelli_display_transfer_init(JelliDisplayTransfer *transfer);
/* Startup/main-task only. Prepare the BSP without starting its LVGL worker.
 * Attach callbacks and create the canvas, unlock, then call esp_lv_adapter_start. */
lv_display_t *jelli_display_prepare(void);
/* Call with the BSP display mutex held, after registering the display. */
void jelli_display_transfer_attach(JelliDisplayTransfer *transfer, lv_display_t *display);
/* Startup only, without the display mutex held. LVGL owns the first refresh. */
esp_err_t jelli_display_transfer_wait_ready(const JelliDisplayTransfer *transfer);
#endif

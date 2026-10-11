#ifndef JELLI_DISPLAY_TRANSFER_H
#define JELLI_DISPLAY_TRANSFER_H
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include <stdint.h>

typedef struct {
    void *pixels;
    esp_lcd_panel_handle_t panel;
    uint32_t submitted, failed;
} JelliDisplayTransfer;

/* Reserve a single internal DMA strip before starting the BSP and Wi-Fi.
 * The LVGL task owns it until the panel completion callback releases a flush. */
void jelli_display_transfer_init(JelliDisplayTransfer *transfer);
/* Call with the BSP display mutex held, after registering the display. */
void jelli_display_transfer_attach(JelliDisplayTransfer *transfer, lv_display_t *display);
#endif

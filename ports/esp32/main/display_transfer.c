#include "display_transfer.h"
#include "jelli/engine.h"
#include "esp_lv_adapter_display.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

/* Pinned Waveshare BSP 3.0.1 uses 466 x 50 pixel LVGL partial draw buffers.
 * Narrow flushes may be taller; bound the total bytes, not their height. */
enum { TRANSFER_BYTES = JELLI_WIDTH * 50 * sizeof(uint16_t) };

void jelli_display_transfer_init(JelliDisplayTransfer *transfer)
{
    transfer->pixels =
        heap_caps_aligned_alloc(4u, TRANSFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(transfer->pixels ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI("jelli_display", "Reserved %u internal DMA bytes for panel transfers",
             (unsigned)TRANSFER_BYTES);
}

static esp_err_t draw_bitmap(lv_display_t *display, esp_lcd_panel_handle_t panel, int left, int top,
                             int right, int bottom, const void *pixels, void *ctx)
{
    (void)display;
    JelliDisplayTransfer *transfer = ctx;
    if (!pixels || left < 0 || top < 0 || right <= left || bottom <= top || right > JELLI_WIDTH ||
        bottom > JELLI_HEIGHT) {
        ++transfer->failed;
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = (size_t)(right - left) * (size_t)(bottom - top) * sizeof(uint16_t);
    if (bytes > TRANSFER_BYTES) {
        ++transfer->failed;
        return ESP_ERR_INVALID_SIZE;
    }
    /* The adapter already packed rows and swapped RGB565 bytes. LVGL 9.3
     * waits for the previous flush before calling us, even with two draw
     * buffers. The BSP's existing DMA-complete ISR releases this flush;
     * never signal completion here or reuse the strip before that callback. */
    memcpy(transfer->pixels, pixels, bytes);
    esp_err_t result = esp_lcd_panel_draw_bitmap(panel, left, top, right, bottom, transfer->pixels);
    if (result == ESP_OK)
        ++transfer->submitted;
    else
        ++transfer->failed;
    return result;
}

void jelli_display_transfer_attach(JelliDisplayTransfer *transfer, lv_display_t *display)
{
    const esp_lv_adapter_draw_bitmap_callbacks_t callbacks = {.custom_draw_bitmap = draw_bitmap};
    ESP_ERROR_CHECK(esp_lv_adapter_set_draw_bitmap_callbacks(display, &callbacks, transfer));
}

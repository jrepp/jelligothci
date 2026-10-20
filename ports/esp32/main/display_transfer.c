#include "display_transfer.h"
#include "pipeline_profile.h"
#include "jelli/engine.h"
#include "esp_lv_adapter_display.h"
#include "esp_lv_adapter.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* Pinned Waveshare BSP 3.0.1 uses 466 x 50 pixel LVGL partial draw buffers.
 * Narrow flushes may be taller; bound the total bytes, not their height. */

/* Only app_main accesses these flags, before the LVGL worker exists. The BSP
 * starts that worker before returning its display; intercept just that request
 * so no default PSRAM flush races installation of our reserved DMA strip. */
static bool preparing, start_requested;
esp_err_t __real_esp_lv_adapter_start(void);
esp_err_t __wrap_esp_lv_adapter_start(void);

esp_err_t __wrap_esp_lv_adapter_start(void)
{
    if (preparing) {
        start_requested = true;
        return ESP_OK;
    }
    return __real_esp_lv_adapter_start();
}

lv_display_t *jelli_display_prepare(void)
{
    ESP_ERROR_CHECK(preparing || start_requested ? ESP_ERR_INVALID_STATE : ESP_OK);
    preparing = true;
    lv_display_t *display = bsp_display_start();
    preparing = false;
    ESP_ERROR_CHECK(display && start_requested ? ESP_OK : ESP_ERR_INVALID_STATE);
    ESP_LOGI("jelli_display", "BSP prepared; LVGL worker deferred until DMA callback is attached");
    return display;
}

/* This startup reservation requires aligned internal DMA-capable memory and
 * stays owned through each completion callback; proposed storage changes:
 * docs-cms/rfcs/rfc-009-bounded-memory-systems-and-adoption.md#foundation-contracts */
void jelli_display_transfer_init(JelliDisplayTransfer *transfer)
{
    transfer->pixels =
        heap_caps_aligned_alloc(4u, JELLI_TRANSFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(transfer->pixels ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI("jelli_display", "Reserved %u internal DMA bytes for panel transfers",
             (unsigned)JELLI_TRANSFER_BYTES);
}

static esp_err_t draw_bitmap(lv_display_t *display, esp_lcd_panel_handle_t panel, int left, int top,
                             int right, int bottom, const void *pixels, void *ctx)
{
    (void)display;
    JelliDisplayTransfer *transfer = ctx;
    transfer->panel = panel;
    if (!pixels || left < 0 || top < 0 || right <= left || bottom <= top || right > JELLI_WIDTH ||
        bottom > JELLI_HEIGHT) {
        ++transfer->failed;
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = (size_t)(right - left) * (size_t)(bottom - top) * sizeof(uint16_t);
    if (bytes > JELLI_TRANSFER_BYTES) {
        ++transfer->failed;
        return ESP_ERR_INVALID_SIZE;
    }
    /* The adapter already packed rows and swapped RGB565 bytes. LVGL 9.3
     * waits for the previous flush before calling us, even with two draw
     * buffers. The BSP's existing DMA-complete ISR releases this flush;
     * never signal completion here or reuse the strip before that callback. */
    uint64_t start = jelli_profile_now();
    memcpy(transfer->pixels, pixels, bytes);
    jelli_profile_record(JELLI_PROFILE_STAGE, start, (uint32_t)bytes);
    jelli_profile_submit();
    esp_err_t result = esp_lcd_panel_draw_bitmap(panel, left, top, right, bottom, transfer->pixels);
    if (result == ESP_OK)
        ++transfer->submitted;
    else {
        jelli_profile_submit_failed();
        ++transfer->failed;
    }
    return result;
}

void jelli_display_transfer_attach(JelliDisplayTransfer *transfer, lv_display_t *display)
{
    const esp_lv_adapter_draw_bitmap_callbacks_t callbacks = {.custom_draw_bitmap = draw_bitmap};
    ESP_ERROR_CHECK(esp_lv_adapter_set_draw_bitmap_callbacks(display, &callbacks, transfer));
    jelli_profile_attach(display);
}

esp_err_t jelli_display_transfer_wait_ready(const JelliDisplayTransfer *transfer)
{
    const int64_t deadline = esp_timer_get_time() + INT64_C(2000000);
    int64_t remaining;
    while ((remaining = deadline - esp_timer_get_time()) >= 1000) {
        esp_err_t result = bsp_display_lock((uint32_t)(remaining / 1000));
        if (result != ESP_OK)
            return result;
        bool ready = transfer->panel && transfer->submitted;
        bool failed = transfer->failed != 0u;
        bsp_display_unlock();
        if (failed)
            return ESP_FAIL;
        if (ready)
            return ESP_OK;
        vTaskDelay(1);
    }
    return ESP_ERR_TIMEOUT;
}

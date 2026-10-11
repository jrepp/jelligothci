#include "pipeline_profile.h"
#include "display_output.h"
#include "swap_copy.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_lcd_co5300.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { PAIRS = 16, FRAME_BYTES = JELLI_WIDTH * JELLI_HEIGHT * sizeof(uint16_t) };

static bool equivalent(const uint8_t *dest, const uint8_t *source, size_t pixels)
{
    for (size_t i = 0; i < pixels * 2u; ++i)
        if (dest[i] != source[i ^ 1u])
            return false;
    return true;
}

static bool measure(JelliDisplayOutput *output, size_t pixels, uint64_t elapsed[2])
{
    uint8_t *canvas = (uint8_t *)output->canvas_pixels;
    const uint8_t *source = (const uint8_t *)output->engine_pixels;
    bool equal = true;
    for (unsigned pair = 0; pair < PAIRS; ++pair) {
        for (unsigned pass = 0; pass < 2u; ++pass) {
            unsigned method = (pair + pass) % 2u;
            memcpy(canvas, source, pixels * 2u);
            uint64_t start = (uint64_t)esp_timer_get_time();
            if (method)
                jelli_swap_copy(output->transfer.pixels, canvas, pixels);
            else {
                lv_draw_sw_rgb565_swap(canvas, (uint32_t)pixels);
                memcpy(output->transfer.pixels, canvas, pixels * 2u);
            }
            elapsed[method] += (uint64_t)esp_timer_get_time() - start;
            equal = equivalent(output->transfer.pixels, source, pixels) && equal;
        }
    }
    return equal;
}

/* Power profile only. Drain the existing transfer before borrowing its DMA
 * strip; no extra buffer, altered ownership, or per-trial allocation. */
void jelli_staging_benchmark(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id)
{
    uint64_t elapsed[3][2] = {{0}};
    const size_t pixels[] = {36u, JELLI_WIDTH, JELLI_WIDTH * 50u};
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    bool ready = output->brightness_known && output->transfer.panel;
    esp_err_t result = ready ? esp_lcd_panel_co5300_set_brightness(
                                   output->transfer.panel, (uint8_t)output->brightness_percent)
                             : ESP_ERR_INVALID_STATE;
    if (result != ESP_OK) {
        bsp_display_unlock();
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"drain\"}");
        return;
    }
    bool equal = true;
    for (unsigned kind = 0; kind < 3u; ++kind)
        equal = measure(output, pixels[kind], elapsed[kind]) && equal;
    memcpy(output->canvas_pixels, output->engine_pixels, FRAME_BYTES);
    lv_obj_invalidate(output->canvas);
    bsp_display_unlock();
    char body[384];
    int size = snprintf(body, sizeof(body),
                        "{\"ok\":%s,\"pairs\":%u,\"pixels\":[36,466,23300],"
                        "\"small_us\":[%" PRIu64 ",%" PRIu64 "],"
                        "\"row_us\":[%" PRIu64 ",%" PRIu64 "],"
                        "\"strip_us\":[%" PRIu64 ",%" PRIu64 "]}",
                        equal ? "true" : "false", (unsigned)PAIRS, elapsed[0][0], elapsed[0][1],
                        elapsed[1][0], elapsed[1][1], elapsed[2][0], elapsed[2][1]);
    if (size > 0 && (size_t)size < sizeof(body))
        jelli_debug_response(debug, id, body);
}

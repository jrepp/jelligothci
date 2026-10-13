#include "display_memory_trial.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_lv_adapter_display.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_lcd_co5300.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

enum { PAIRS = 8, GUARD = 4 };
static _Alignas(16) unsigned char internal_source[1024];
static uint64_t elapsed[4][3];
static char report[1400];
_Static_assert(sizeof(internal_source) + sizeof(elapsed) + sizeof(report) <= 2560u,
               "Memory trial scratch exceeds 2.5 KiB");
static const size_t lengths[] = {72, JELLI_WIDTH * 2u, JELLI_TRANSFER_BYTES - 2u * GUARD};
static unsigned draw_rows;
static bool draw_psram;

lv_display_t *__real_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *cfg);
lv_display_t *__wrap_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *cfg);
lv_display_t *__wrap_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *cfg)
{
    esp_lv_adapter_display_config_t trial = *cfg;
#if CONFIG_JELLI_MEMORY_TRIAL > 0
    trial.profile.buffer_height = 8;
    trial.profile.use_psram = CONFIG_JELLI_MEMORY_TRIAL == 1;
#endif
    draw_rows = trial.profile.buffer_height;
    draw_psram = trial.profile.use_psram;
    return __real_esp_lv_adapter_register_display(&trial);
}

static bool copy_case(JelliDisplayOutput *output, unsigned path, unsigned size)
{
    size_t bytes = lengths[size];
    unsigned char *dma = output->transfer.pixels;
    const unsigned char *engine = (const unsigned char *)output->engine_pixels;
    unsigned char *canvas = (unsigned char *)output->canvas_pixels;
    unsigned char *dest = (path & 1u) ? dma : canvas;
    const unsigned char *source = path < 2u ? engine : path == 2u ? dma + GUARD : internal_source;
    if (path == 3u && bytes > sizeof(internal_source))
        return true; /* No second internal strip allocation just for this probe. */
    if (path == 2u)
        memcpy(dma + GUARD, engine, bytes);
    if (path == 3u)
        memcpy(internal_source, engine, bytes);
    memset(dest, 0xa5, bytes + 2u * GUARD);
    uint64_t start = (uint64_t)esp_timer_get_time();
    memcpy(dest + GUARD, source, bytes);
    elapsed[path][size] += (uint64_t)esp_timer_get_time() - start;
    bool equal = !memcmp(dest + GUARD, source, bytes);
    for (unsigned n = 0; n < GUARD; ++n)
        equal = equal && dest[n] == 0xa5 && dest[GUARD + bytes + n] == 0xa5;
    return equal;
}

static bool measure(JelliDisplayOutput *output)
{
    bool equal = true;
    memset(elapsed, 0, sizeof(elapsed));
    for (unsigned pair = 0; pair < PAIRS; ++pair)
        for (unsigned step = 0; step < 4u; ++step)
            for (unsigned size = 0; size < 3u; ++size) {
                unsigned path = pair % 2u ? 3u - step : step;
                equal = copy_case(output, path, size) && equal;
            }
    memcpy(output->canvas_pixels, output->engine_pixels,
           JELLI_WIDTH * JELLI_HEIGHT * sizeof(uint16_t));
    lv_obj_invalidate(output->canvas);
    return equal;
}

static void memory_report(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id, bool equal,
                          bool measured)
{
    const lv_draw_buf_t *draw = lv_display_get_buf_active(lv_obj_get_display(output->canvas));
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    int size = snprintf(
        report, sizeof(report),
        "{\"ok\":%s,\"mode\":%d,\"rows\":%u,\"draw_psram\":%s,"
        "\"draw_external\":%s,\"draw_dma\":%s,\"draw_bytes\":%u,\"draw_stride\":%u,"
        "\"engine_external\":%s,\"canvas_external\":%s,\"stage_internal\":%s,\"stage_dma\":%s,"
        "\"stage_bytes\":%u,\"internal_free\":%u,\"internal_largest\":%u,\"internal_min\":%u,"
        "\"psram_free\":%u,\"pairs\":%u,\"bytes\":[72,932,%u],\"us\":["
        "[%llu,%llu,%llu],[%llu,%llu,%llu],[%llu,%llu,%llu],[%llu,%llu,null]]}",
        equal ? "true" : "false", CONFIG_JELLI_MEMORY_TRIAL, draw_rows,
        draw_psram ? "true" : "false", esp_ptr_external_ram(draw->data) ? "true" : "false",
        esp_ptr_dma_capable(draw->data) ? "true" : "false", (unsigned)draw->data_size,
        (unsigned)draw->header.stride,
        esp_ptr_external_ram(output->engine_pixels) ? "true" : "false",
        esp_ptr_external_ram(output->canvas_pixels) ? "true" : "false",
        esp_ptr_internal(output->transfer.pixels) ? "true" : "false",
        esp_ptr_dma_capable(output->transfer.pixels) ? "true" : "false",
        (unsigned)JELLI_TRANSFER_BYTES, (unsigned)heap_caps_get_free_size(caps),
        (unsigned)heap_caps_get_largest_free_block(caps),
        (unsigned)heap_caps_get_minimum_free_size(caps),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), measured ? PAIRS : 0u,
        (unsigned)lengths[2], (unsigned long long)elapsed[0][0], (unsigned long long)elapsed[0][1],
        (unsigned long long)elapsed[0][2], (unsigned long long)elapsed[1][0],
        (unsigned long long)elapsed[1][1], (unsigned long long)elapsed[1][2],
        (unsigned long long)elapsed[2][0], (unsigned long long)elapsed[2][1],
        (unsigned long long)elapsed[2][2], (unsigned long long)elapsed[3][0],
        (unsigned long long)elapsed[3][1]);
    if (size > 0 && (size_t)size < sizeof(report))
        jelli_debug_response(debug, id, report);
}

void jelli_display_memory_trial(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id,
                                bool benchmark)
{
    if (bsp_display_lock(2000) != ESP_OK) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"lock\"}");
        return;
    }
    bool equal = true;
    if (benchmark) {
        esp_err_t drained = output->brightness_known && output->transfer.panel
                                ? esp_lcd_panel_co5300_set_brightness(
                                      output->transfer.panel, (uint8_t)output->brightness_percent)
                                : ESP_ERR_INVALID_STATE;
        equal = drained == ESP_OK && measure(output);
    }
    memory_report(output, debug, id, equal, benchmark);
    bsp_display_unlock();
}

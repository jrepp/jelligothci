#include "display_output.h"
#include "network.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { FRAME_PIXELS = JELLI_WIDTH * JELLI_HEIGHT, GUARD_PIXELS = 16, GUARD = 0xa55a };
static const char *TAG = "jelli_display";

static uint16_t *allocate_frame(void)
{
    uint16_t *raw = heap_caps_calloc(FRAME_PIXELS + 2u * GUARD_PIXELS, sizeof(uint16_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(raw ? ESP_OK : ESP_ERR_NO_MEM);
    for (unsigned i = 0; i < GUARD_PIXELS; ++i) {
        raw[i] = GUARD;
        raw[GUARD_PIXELS + FRAME_PIXELS + i] = GUARD;
    }
    return raw + GUARD_PIXELS;
}

static bool intact(const uint16_t *pixels)
{
    const uint16_t *raw = pixels - GUARD_PIXELS;
    for (unsigned i = 0; i < GUARD_PIXELS; ++i)
        if (raw[i] != GUARD || raw[GUARD_PIXELS + FRAME_PIXELS + i] != GUARD)
            return false;
    return true;
}

void jelli_display_output_init(JelliDisplayOutput *output)
{
    jelli_display_transfer_init(&output->transfer);
    output->engine_pixels = allocate_frame();
    output->canvas_pixels = allocate_frame();
}

/* Called after copying, under the display mutex; never reads a changing canvas. */
static void check_buffers(JelliDisplayOutput *output, uint64_t now)
{
    if (output->checks && now >= output->checked_ms && now - output->checked_ms < 5000u)
        return;
    output->checked_ms = now;
    ++output->checks;
    output->guards_ok = intact(output->engine_pixels) && intact(output->canvas_pixels);
    output->buffers_equal =
        !memcmp(output->engine_pixels, output->canvas_pixels, FRAME_PIXELS * sizeof(uint16_t));
    output->heap_ok = heap_caps_check_integrity_all(true);
    output->submitted = output->transfer.submitted;
    output->failed = output->transfer.failed;
    output->stack_free = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    if (!output->buffers_equal)
        ++output->mismatches;
    ESP_LOGI(TAG, "check=%" PRIu32 " equal=%u guards=%u heap=%u stack_free=%u mismatches=%" PRIu32,
             output->checks, output->buffers_equal, output->guards_ok, output->heap_ok,
             output->stack_free, output->mismatches);
    ESP_ERROR_CHECK(output->guards_ok && output->heap_ok ? ESP_OK : ESP_ERR_INVALID_STATE);
}

void jelli_display_output_present(JelliDisplayOutput *output, const JelliSurface *surface,
                                  uint64_t now)
{
    JelliRect r = surface->damage;
    bool valid = surface->pixels == output->engine_pixels && surface->width == JELLI_WIDTH &&
                 surface->height == JELLI_HEIGHT && surface->stride == JELLI_WIDTH &&
                 r.x <= JELLI_WIDTH && r.y <= JELLI_HEIGHT && r.width <= JELLI_WIDTH - r.x &&
                 r.height <= JELLI_HEIGHT - r.y;
    ESP_ERROR_CHECK(valid ? ESP_OK : ESP_ERR_INVALID_SIZE);
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    /* Explicit refresh retransmits the existing canvas; it does not repair or
     * overwrite it, so a copy mismatch remains available for diagnosis. */
    for (unsigned y = r.y; r.width && y < r.y + r.height; ++y)
        memcpy(output->canvas_pixels + y * JELLI_WIDTH + r.x,
               surface->pixels + y * surface->stride + r.x, r.width * sizeof(uint16_t));
    check_buffers(output, now);
    if (output->refresh_requested) {
        r = (JelliRect){0, 0, JELLI_WIDTH, JELLI_HEIGHT};
        output->refresh_requested = false;
    }
    if (r.width && r.height) {
        lv_area_t area = {(int32_t)r.x, (int32_t)r.y, (int32_t)(r.x + r.width - 1u),
                          (int32_t)(r.y + r.height - 1u)};
        lv_obj_invalidate_area(output->canvas, &area);
    }
    bsp_display_unlock();
}

bool jelli_display_output_command(void *ctx, JelliDebug *debug, const JelliPetEngine *engine,
                                  uint32_t id, char **words, unsigned count)
{
    JelliDisplayOutput *output = ctx;
    if (count < 3u || strcmp(words[2], "display"))
        return jelli_network_command(NULL, debug, engine, id, words, count);
    if (count == 4u && !strcmp(words[3], "refresh")) {
        output->refresh_requested = true;
        jelli_debug_response(debug, id, "{\"ok\":true,\"pending\":true}");
    } else if (count == 3u) {
        char body[320];
        int size = snprintf(
            body, sizeof(body),
            "{\"ok\":true,\"checks\":%" PRIu32 ",\"mismatches\":%" PRIu32
            ",\"buffers_equal\":%s,\"guards_ok\":%s,\"heap_ok\":%s,"
            "\"stack_free_bytes\":%u,\"transfers\":%" PRIu32 ",\"transfer_failures\":%" PRIu32 "}",
            output->checks, output->mismatches, output->buffers_equal ? "true" : "false",
            output->guards_ok ? "true" : "false", output->heap_ok ? "true" : "false",
            output->stack_free, output->submitted, output->failed);
        if (size > 0 && (size_t)size < sizeof(body))
            jelli_debug_response(debug, id, body);
    } else
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax\"}");
    return true;
}

#include "render_benchmark.h"
#include "../../../tools/bench/rect_workload.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { PIXELS = JELLI_WIDTH * JELLI_HEIGHT, PAIRS = 8 };

static uint32_t checksum(const uint16_t *pixels)
{
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < PIXELS; ++i)
        hash = (hash ^ pixels[i]) * 16777619u;
    return hash;
}

/* Diagnostic only. Canvas is borrowed under the mutex and restored before
 * release; the engine frame stays untouched. Cache setup is equal per method. */
void jelli_render_benchmark(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id)
{
    uint64_t elapsed[3][2] = {{0}};
    bool equal = true;
    JelliSurface surface = {.pixels = output->canvas_pixels,
                            .width = JELLI_WIDTH,
                            .height = JELLI_HEIGHT,
                            .stride = JELLI_WIDTH};
    Canvas canvas = {&surface, 0, 0, JELLI_WIDTH, JELLI_HEIGHT, 0, false, NULL};
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    for (unsigned kind = 0; kind < 3u; ++kind) {
        for (unsigned pair = 0; pair < PAIRS; ++pair) {
            uint32_t hashes[2] = {0};
            for (unsigned pass = 0; pass < 2u; ++pass) {
                unsigned method = (pair + pass) % 2u;
                memcpy(output->canvas_pixels, output->engine_pixels, PIXELS * sizeof(uint16_t));
                int64_t start = esp_timer_get_time();
                jelli_rect_workload(&canvas, kind,
                                    method ? jelli_canvas_rect : jelli_rect_reference);
                elapsed[kind][method] += (uint64_t)(esp_timer_get_time() - start);
                hashes[method] = checksum(output->canvas_pixels);
            }
            equal = equal && hashes[0] == hashes[1];
        }
    }
    memcpy(output->canvas_pixels, output->engine_pixels, PIXELS * sizeof(uint16_t));
    bsp_display_unlock();
    char body[384];
    int size = snprintf(body, sizeof(body),
                        "{\"ok\":%s,\"pairs\":%u,"
                        "\"small_us\":[%" PRIu64 ",%" PRIu64 "],"
                        "\"full_us\":[%" PRIu64 ",%" PRIu64 "],"
                        "\"edge_us\":[%" PRIu64 ",%" PRIu64 "]}",
                        equal ? "true" : "false", (unsigned)PAIRS, elapsed[0][0], elapsed[0][1],
                        elapsed[1][0], elapsed[1][1], elapsed[2][0], elapsed[2][1]);
    if (size > 0 && (size_t)size < sizeof(body))
        jelli_debug_response(debug, id, body);
}

#include "render_benchmark.h"
#ifdef CONFIG_JELLI_POWER_EXPERIMENTS
#include "span_rect.h"
#endif
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
static void benchmark(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id,
                      JelliRectDraw reference, JelliRectDraw candidate)
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
                jelli_rect_workload(&canvas, kind, method ? candidate : reference);
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

void jelli_render_benchmark(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id)
{
    benchmark(output, debug, id, jelli_rect_reference, jelli_canvas_rect);
}

#ifdef CONFIG_JELLI_POWER_EXPERIMENTS
/* Exact single-rectangle oracle, independent pixel predicate and untouched
 * engine source. No extra framebuffer; entire canvas checked outside timing. */
static bool qualify_rect(JelliDisplayOutput *output)
{
    static const int cases[][4] = {{0, 0, 466, 466},  {0, 0, 0, 0},      {-17, 200, 36, 6},
                                   {440, 210, 36, 6}, {200, -4, 66, 17}, {200, 450, 66, 30},
                                   {130, 130, 6, 6},  {199, 199, 65, 17}};
    JelliSurface surface = {.pixels = output->canvas_pixels,
                            .width = JELLI_WIDTH,
                            .height = JELLI_HEIGHT,
                            .stride = JELLI_WIDTH};
    for (unsigned dim = 0; dim < 2u; ++dim)
        for (unsigned n = 0; n < sizeof(cases) / sizeof(cases[0]); ++n) {
            Canvas c = {&surface, dim ? 7 : 0, dim ? 3 : 0, 461, 463, 0, dim != 0, NULL};
            int x = cases[n][0], y = cases[n][1], w = cases[n][2], h = cases[n][3];
            uint16_t color = dim ? (uint16_t)((0xabcdu >> 1) & 0x7befu) : 0xabcdu;
            memcpy(output->canvas_pixels, output->engine_pixels, PIXELS * sizeof(uint16_t));
            jelli_canvas_rect_simd(&c, x, y, w, h, 0xabcdu);
            for (int py = 0; py < JELLI_HEIGHT; ++py)
                for (int px = 0; px < JELLI_WIDTH; ++px) {
                    int dx = 2 * px - 465, dy = 2 * py - 465;
                    bool drawn = px >= x && px < x + w && py >= y && py < y + h && px >= c.left &&
                                 px < c.right && py >= c.top && py < c.bottom &&
                                 dx * dx + dy * dy <= 466 * 466;
                    unsigned i = (unsigned)py * JELLI_WIDTH + (unsigned)px;
                    if (output->canvas_pixels[i] != (drawn ? color : output->engine_pixels[i]))
                        return false;
                }
        }
    return true;
}

void jelli_render_simd_benchmark(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id,
                                 bool simd)
{
    if (bsp_display_lock(2000) != ESP_OK) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"lock\"}");
        return;
    }
    jelli_span_rect_mode(simd);
    bool equal = qualify_rect(output);
    memcpy(output->canvas_pixels, output->engine_pixels, PIXELS * sizeof(uint16_t));
    bsp_display_unlock();
    if (!equal) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"pixels\"}");
        return;
    }
    benchmark(output, debug, id, jelli_canvas_rect, jelli_canvas_rect_simd);
}
#endif

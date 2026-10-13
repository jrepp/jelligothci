#include "span_benchmark.h"
#include "span_kernels.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

enum { PAIRS = 8, REPEATS = 16, CAPACITY = 528, GUARD = 8, PSRAM_CAPACITY = 23328 };
typedef void (*Span)(uint16_t *, size_t, uint16_t, unsigned);
static const Span kernels[2][2] = {{jelli_span_fill_scalar, jelli_span_fill_simd},
                                   {jelli_span_blend_scalar, jelli_span_blend_packed}};
/* 1056 bytes internal scratch; existing canvas lends the PSRAM test area.
 * Fixed results/report avoid adding large arrays to the main task stack. */
static _Alignas(16) uint16_t scratch[CAPACITY];
static uint64_t elapsed[2][2][4][2];
static uint64_t endpoints[2][2][2];
static char report[1500];
_Static_assert(sizeof(scratch) + sizeof(elapsed) + sizeof(endpoints) + sizeof(report) <= 3072,
               "Span benchmark scratch exceeds 3 KiB budget");
static const size_t counts[2][4] = {{6, 36, 466, 512}, {6, 36, 466, 23300}};

static uint16_t seed(size_t index) { return (uint16_t)(index * 40503u + 91u); }

static void reset(uint16_t *buffer, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        buffer[i] = seed(i);
}

static bool check(uint16_t *buffer, size_t capacity, size_t offset, size_t count, unsigned kind,
                  unsigned alpha, uint16_t color)
{
    reset(buffer, capacity);
    kernels[kind][1](buffer + offset, count, color, alpha);
    for (size_t i = 0; i < capacity; ++i) {
        uint16_t expected = seed(i);
        if (i >= offset && i - offset < count)
            expected = kind ? jelli_span_blend_pixel(expected, color, alpha) : color;
        if (buffer[i] != expected)
            return false;
    }
    return true;
}

static bool qualify(uint16_t *buffer, size_t capacity)
{
    static const size_t lengths[] = {0, 1, 6, 7, 8, 9, 15, 16, 17, 36, 466};
    static const uint16_t colors[] = {0, 0xffff, 0xf81f, 0x07e0, 0x95a6};
    for (unsigned kind = 0; kind < 2u; ++kind)
        for (unsigned offset = GUARD; offset < GUARD + 8u; ++offset)
            for (unsigned length = 0; length < sizeof(lengths) / sizeof(lengths[0]); ++length)
                for (unsigned alpha = 0; alpha <= 16u; ++alpha)
                    if (!check(buffer, CAPACITY, offset, lengths[length], kind, alpha,
                               colors[alpha % 5u]))
                        return false;
    /* Exercise the largest span as well as every short alignment/tail above. */
    return check(buffer, capacity, GUARD, capacity - 2u * GUARD, 0, 16, 0xa55a) &&
           check(buffer, capacity, GUARD, capacity - 2u * GUARD, 1, 7, 0x07e0);
}

static void measure(uint16_t *buffer, size_t count, unsigned kind, unsigned alpha, uint64_t out[2])
{
    for (unsigned pair = 0; pair < PAIRS; ++pair) {
        /* All eight 16-bit alignments receive one pair; order alternates. */
        uint16_t *dest = buffer + GUARD + pair;
        for (unsigned pass = 0; pass < 2u; ++pass) {
            unsigned method = (pair + pass) % 2u;
            reset(dest, count);
            int64_t start = esp_timer_get_time();
            for (unsigned repeat = 0; repeat < REPEATS; ++repeat)
                kernels[kind][method](dest, count, (uint16_t)(0x95a6u + repeat), alpha);
            out[method] += (uint64_t)(esp_timer_get_time() - start);
        }
    }
}

static void result(JelliDebug *debug, uint32_t id, bool equal)
{
    size_t used = 0;
    int size = snprintf(report, sizeof(report),
                        "{\"ok\":%s,\"pairs\":%u,\"repeats\":%u,\"alpha\":7,"
                        "\"counts\":[[6,36,466,512],[6,36,466,23300]],\"us\":[",
                        equal ? "true" : "false", (unsigned)PAIRS, (unsigned)REPEATS);
    if (size < 0 || (size_t)size >= sizeof(report))
        return;
    used = (size_t)size;
    for (unsigned memory = 0; memory < 2u; ++memory)
        for (unsigned kind = 0; kind < 2u; ++kind)
            for (unsigned n = 0; n < 4u; ++n) {
                size = snprintf(report + used, sizeof(report) - used, "%s[%llu,%llu]",
                                used && (memory || kind || n) ? "," : "",
                                (unsigned long long)elapsed[memory][kind][n][0],
                                (unsigned long long)elapsed[memory][kind][n][1]);
                if (size < 0 || (size_t)size >= sizeof(report) - used)
                    return;
                used += (size_t)size;
            }
    size = snprintf(report + used, sizeof(report) - used,
                    "],\"endpoint_pixels\":466,\"endpoint_alpha\":[0,16],"
                    "\"endpoint_us\":[[%llu,%llu],[%llu,%llu],[%llu,%llu],[%llu,%llu]]}",
                    (unsigned long long)endpoints[0][0][0], (unsigned long long)endpoints[0][0][1],
                    (unsigned long long)endpoints[0][1][0], (unsigned long long)endpoints[0][1][1],
                    (unsigned long long)endpoints[1][0][0], (unsigned long long)endpoints[1][0][1],
                    (unsigned long long)endpoints[1][1][0], (unsigned long long)endpoints[1][1][1]);
    if (size < 0 || (size_t)size >= sizeof(report) - used)
        return;
    jelli_debug_response(debug, id, report);
}

void jelli_span_benchmark(JelliDisplayOutput *output, JelliDebug *debug, uint32_t id)
{
    if (bsp_display_lock(2000) != ESP_OK) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"lock\"}");
        return;
    }
    memset(elapsed, 0, sizeof(elapsed));
    memset(endpoints, 0, sizeof(endpoints));
    bool equal = qualify(scratch, CAPACITY) && qualify(output->canvas_pixels, PSRAM_CAPACITY);
    if (equal) {
        for (unsigned memory = 0; memory < 2u; ++memory)
            for (unsigned kind = 0; kind < 2u; ++kind)
                for (unsigned size = 0; size < 4u; ++size)
                    measure(memory ? output->canvas_pixels : scratch, counts[memory][size], kind, 7,
                            elapsed[memory][kind][size]);
        for (unsigned memory = 0; memory < 2u; ++memory)
            for (unsigned alpha = 0; alpha < 2u; ++alpha)
                measure(memory ? output->canvas_pixels : scratch, 466, 1, alpha * 16u,
                        endpoints[memory][alpha]);
    }
    memcpy(output->canvas_pixels, output->engine_pixels,
           JELLI_WIDTH * JELLI_HEIGHT * sizeof(uint16_t));
    lv_obj_invalidate(output->canvas);
    bsp_display_unlock();
    result(debug, id, equal);
}

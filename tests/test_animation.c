#include "jelli/animation.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void endpoints_and_retarget(void)
{
    JelliColorTween t = {.value = {0, 255, 80}};
    jelli_color_start(&t, (JelliColor){255, 0, 80}, 300);
    jelli_color_advance(&t, 150);
    CHECK(t.value.r == 127 && t.value.g == 127 && t.value.b == 80);
    jelli_color_advance(&t, 149);
    CHECK(t.value.r < 255 && t.elapsed_ms == 299);
    jelli_color_advance(&t, 1);
    CHECK(t.value.r == 255 && t.value.g == 0 && t.elapsed_ms == 300);
    jelli_color_advance(&t, UINT64_MAX);
    CHECK(t.value.r == 255 && t.elapsed_ms == 300);
    jelli_color_start(&t, (JelliColor){0, 255, 0}, 300);
    jelli_color_advance(&t, 150);
    JelliColor previous = t.value;
    jelli_color_start(&t, (JelliColor){200, 100, 50}, 300);
    CHECK(t.value.r == previous.r && t.value.g == previous.g && t.value.b == previous.b);
    jelli_color_advance(&t, 300);
    CHECK(t.value.r == 200 && t.value.g == 100 && t.value.b == 50);
}

static void cadence_and_limits(void)
{
    JelliColorTween fast = {.value = {10, 220, 50}};
    JelliColorTween slow = fast;
    jelli_color_start(&fast, (JelliColor){210, 20, 150}, 300);
    jelli_color_start(&slow, (JelliColor){210, 20, 150}, 300);
    for (unsigned i = 0; i < 15; ++i)
        jelli_color_advance(&fast, 10);
    jelli_color_advance(&slow, 150);
    CHECK(fast.value.r == 110 && fast.value.g == 120 && fast.value.b == 100);
    CHECK(jelli_rgb565(fast.value) == jelli_rgb565(slow.value));
    jelli_color_start(&fast, (JelliColor){255, 255, 255}, 0);
    CHECK(jelli_rgb565(fast.value) == 0xffff);
    jelli_color_advance(&fast, UINT64_MAX);
    CHECK(jelli_rgb565(fast.value) == 0xffff);
    jelli_color_start(&fast, (JelliColor){0, 0, 0}, UINT32_MAX);
    jelli_color_advance(&fast, UINT32_MAX - 1u);
    CHECK(fast.elapsed_ms == UINT32_MAX - 1u);
    jelli_color_advance(&fast, UINT64_MAX);
    CHECK(jelli_rgb565(fast.value) == 0 && fast.elapsed_ms == UINT32_MAX);
    CHECK(jelli_rgb565((JelliColor){255, 0, 0}) == 0xf800);
    CHECK(jelli_rgb565((JelliColor){0, 255, 0}) == 0x07e0);
    CHECK(jelli_rgb565((JelliColor){0, 0, 255}) == 0x001f);
}

int main(void)
{
    endpoints_and_retarget();
    cadence_and_limits();
    puts("PASS: bounded color transitions, retargeting, cadence, time limits");
    return 0;
}

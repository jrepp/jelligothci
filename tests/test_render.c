#include "jelli/engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STRIDE = JELLI_WIDTH + 3, PIXELS = STRIDE * JELLI_HEIGHT };
static uint16_t incremental[PIXELS], reference[PIXELS], previous[PIXELS];

static void compare_frame(JelliEngine *e)
{
    memcpy(previous, incremental, sizeof(previous));
    jelli_render(e);
    JelliRect d = e->surface.damage;
    CHECK(d.x <= JELLI_WIDTH && d.width <= JELLI_WIDTH - d.x);
    CHECK(d.y <= JELLI_HEIGHT && d.height <= JELLI_HEIGHT - d.y);
    JelliEngine full = *e;
    full.surface.pixels = reference;
    full.rendered = false;
    jelli_render(&full);
    CHECK(memcmp(incremental, reference, sizeof(reference)) == 0);
    for (unsigned y = 0; y < JELLI_HEIGHT; ++y) {
        for (unsigned x = 0; x < STRIDE; ++x) {
            bool inside = x >= d.x && x - d.x < d.width && y >= d.y && y - d.y < d.height;
            if (!inside)
                CHECK(incremental[y * STRIDE + x] == previous[y * STRIDE + x]);
        }
    }
}

int main(void)
{
    /* Padded rows catch writes outside the drawable width. Buffers remain fixed. */
    memset(incremental, 0xa5, sizeof(incremental));
    memset(reference, 0xa5, sizeof(reference));
    JelliEngine e = {
        .surface = {
            .pixels = incremental, .width = JELLI_WIDTH, .height = JELLI_HEIGHT, .stride = STRIDE}};
    compare_frame(&e);
    CHECK(e.surface.damage.width == JELLI_WIDTH && e.surface.damage.height == JELLI_HEIGHT);
    compare_frame(&e);
    CHECK(e.surface.damage.width == 0 && e.surface.damage.height == 0);
    for (unsigned time = 0; time < 4000; time += 13) {
        e.animation_ms = time;
        for (unsigned i = 0; i < JELLI_SHAPE_COUNT; ++i)
            e.colors[i].value = (JelliColor){(uint8_t)(time % 256u), (uint8_t)(i * 80u), 255};
        compare_frame(&e);
    }
    /* Large motion jumps and then motion alone must erase all previous pixels. */
    e.animation_ms = 2000;
    compare_frame(&e);
    e.animation_ms = 0;
    compare_frame(&e);
    CHECK(e.surface.damage.height == 69);
    compare_frame(&e);
    CHECK(e.surface.damage.width == 0);
    puts("Incremental rendering matches full frames; damage and padding verified.");
    return 0;
}

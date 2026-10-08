#include "jelli/engine.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep checks enabled in Release builds as well. */
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    uint64_t now;
    unsigned presented, notifications, polled;
    JelliInput pending;
    bool has_input, paused, repeat;
} Fake;
static uint64_t now_ms(void *ctx) { return ((Fake *)ctx)->now; }
static bool poll_input(void *ctx, JelliInput *input)
{
    Fake *f = ctx;
    if (!f->has_input)
        return false;
    *input = f->pending;
    f->has_input = f->repeat;
    ++f->polled;
    return true;
}
static void present(void *ctx, const JelliSurface *s)
{
    CHECK(s->pixels != NULL);
    ((Fake *)ctx)->presented++;
}
static void paused(void *ctx, bool value)
{
    Fake *f = ctx;
    f->paused = value;
    f->notifications++;
}
static void send(Fake *f, JelliInputKind kind, int x, int y)
{
    f->pending = (JelliInput){kind, x, y};
    f->has_input = true;
}

static void test_tap_colors(JelliSurface surface)
{
    Fake fake = {0};
    JelliPlatform platform = {&fake, now_ms, poll_input, present, paused};
    JelliEngine e;
    CHECK(jelli_init(&e, platform, surface));
    CHECK(jelli_frame(&e));
    uint16_t original = surface.pixels[150 * surface.stride + 140];
    send(&fake, JELLI_TAP, 233, 233);
    CHECK(jelli_frame(&e) && !e.paused && fake.notifications == 0);
    CHECK(surface.pixels[150 * surface.stride + 140] == original);
    fake.now = 150;
    CHECK(jelli_frame(&e));
    CHECK(e.colors[0].value.r == 180 && e.colors[0].value.g == 160);
    uint16_t midway = surface.pixels[150 * surface.stride + 140];
    CHECK(midway != original);
    /* The prior value was read through the injected clock callback. */
    // cppcheck-suppress redundantAssignment
    fake.now = 100; /* A backward clock does not reverse the transition. */
    CHECK(jelli_frame(&e));
    CHECK(surface.pixels[150 * surface.stride + 140] == midway);
    send(&fake, JELLI_TAP, 233, 233);
    CHECK(jelli_frame(&e));
    CHECK(surface.pixels[150 * surface.stride + 140] == midway);
    send(&fake, JELLI_TOGGLE_PAUSE, 0, 0);
    CHECK(jelli_frame(&e) && e.paused);
    fake.now += 300;
    CHECK(jelli_frame(&e));
    CHECK(e.animation_ms == 150); /* Feedback finishes even while motion is paused. */
    CHECK(e.colors[0].value.r == 255 && e.colors[0].value.g == 174);
    CHECK(e.colors[1].value.r == 127 && e.colors[2].value.r == 106);
    CHECK(surface.pixels[150 * surface.stride + 140] == jelli_rgb565(e.colors[0].to));
    unsigned palette = e.palette;
    send(&fake, JELLI_TAP, 0, 0);
    CHECK(jelli_frame(&e) && e.palette == palette);
    send(&fake, JELLI_TAP, INT_MIN, INT_MAX);
    CHECK(jelli_frame(&e) && e.palette == palette);
    fake.repeat = true;
    unsigned polled = fake.polled;
    send(&fake, JELLI_TAP, 233, 233);
    CHECK(jelli_frame(&e) && fake.polled == polled + 32u);
    fake.repeat = false;
    fake.has_input = false;
    fake.now = UINT64_MAX;
    CHECK(jelli_frame(&e));
    CHECK(e.colors[0].elapsed_ms == JELLI_COLOR_DURATION_MS);
}

int main(void)
{
    /* Padded rows and surrounding canaries detect writes outside the surface. */
    enum { STRIDE = JELLI_WIDTH + 3, COUNT = STRIDE * JELLI_HEIGHT };
    uint16_t *memory = malloc((COUNT + 2) * sizeof(*memory));
    CHECK(memory);
    for (unsigned i = 0; i < COUNT + 2; ++i)
        memory[i] = 0xdead;
    Fake fake = {.now = 100};
    JelliPlatform platform = {&fake, now_ms, poll_input, present, paused};
    JelliSurface surface = {
        .pixels = memory + 1, .width = JELLI_WIDTH, .height = JELLI_HEIGHT, .stride = STRIDE};
    JelliEngine e;
    CHECK(jelli_init(&e, platform, surface));
    CHECK(jelli_frame(&e));
    CHECK(e.animation_ms == 0 && fake.presented == 1);
    CHECK(surface.pixels[0] == 0);                       /* circular clipping */
    CHECK(surface.pixels[150 * STRIDE + 140] == 0x6ebf); /* RGB565 blue square */
    uint16_t background = surface.pixels[302 * STRIDE + 233];
    CHECK(surface.pixels[302 * STRIDE + 143] != background);
    fake.now += 1000;
    CHECK(jelli_frame(&e));
    CHECK(e.animation_ms == 1000);
    CHECK(surface.pixels[302 * STRIDE + 143] == background);
    CHECK(surface.pixels[302 * STRIDE + 233] != background);

    send(&fake, JELLI_TOGGLE_PAUSE, 233, 233);
    CHECK(jelli_frame(&e));
    CHECK(e.paused && fake.paused && fake.notifications == 1);
    fake.now += 5000;
    CHECK(jelli_frame(&e));
    CHECK(e.animation_ms == 1000);
    send(&fake, JELLI_TAP, 0, 0); /* outside round display */
    CHECK(jelli_frame(&e) && e.paused);
    send(&fake, JELLI_TAP, INT_MIN, INT_MAX);
    CHECK(jelli_frame(&e) && e.paused);
    send(&fake, JELLI_TOGGLE_PAUSE, 0, 0);
    CHECK(jelli_frame(&e) && !e.paused);
    fake.now += 1000;
    CHECK(jelli_frame(&e) && e.animation_ms == 2000);
    /* The previous value was consumed via the injected now_ms callback. */
    // cppcheck-suppress redundantAssignment
    fake.now = 0; /* defensive handling of a clock moving backward */
    CHECK(jelli_frame(&e) && e.animation_ms == 2000);
    fake.now = UINT64_MAX;
    CHECK(jelli_frame(&e));
    CHECK(e.animation_ms == (2000 + UINT64_MAX % 4000) % 4000);

    CHECK(memory[0] == 0xdead && memory[COUNT + 1] == 0xdead);
    for (unsigned y = 0; y < JELLI_HEIGHT; ++y)
        for (unsigned x = JELLI_WIDTH; x < STRIDE; ++x)
            CHECK(surface.pixels[y * STRIDE + x] == 0xdead);
    unsigned frames = fake.presented;
    send(&fake, JELLI_QUIT, 0, 0);
    CHECK(!jelli_frame(&e) && !jelli_frame(&e) && fake.presented == frames);
    platform.now_ms = NULL;
    CHECK(!jelli_init(&e, platform, surface));
    platform.now_ms = now_ms;
    surface.stride = 1;
    CHECK(!jelli_init(&e, platform, surface));
    surface.stride = STRIDE;
    test_tap_colors(surface);
    free(memory);
    puts("PASS: injected clock/input/output, rendering, bounds, pause, quit");
    return 0;
}

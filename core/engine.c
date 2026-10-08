#include "jelli/engine.h"
#include <string.h>

static const JelliColor palettes[][JELLI_SHAPE_COUNT] = {
    {{106, 215, 255}, {255, 174, 99}, {127, 235, 175}},
    {{255, 105, 180}, {150, 120, 255}, {255, 220, 90}},
    {{255, 174, 99}, {127, 235, 175}, {106, 215, 255}}};

static bool visible_tap(JelliInput input)
{
    if (input.x < 0 || input.x >= JELLI_WIDTH || input.y < 0 || input.y >= JELLI_HEIGHT)
        return false;
    int dx = 2 * input.x - 465, dy = 2 * input.y - 465;
    return dx * dx + dy * dy <= 466 * 466;
}

static void change_colors(JelliEngine *e)
{
    e->palette = (e->palette + 1u) % (unsigned)(sizeof(palettes) / sizeof(palettes[0]));
    for (unsigned i = 0; i < JELLI_SHAPE_COUNT; ++i)
        jelli_color_start(&e->colors[i], palettes[e->palette][i], JELLI_COLOR_DURATION_MS);
}

bool jelli_init(JelliEngine *e, JelliPlatform platform, JelliSurface surface)
{
    if (!e || !platform.now_ms || !platform.present || !surface.pixels ||
        surface.width != JELLI_WIDTH || surface.height != JELLI_HEIGHT ||
        surface.stride < surface.width)
        return false;
    memset(e, 0, sizeof(*e));
    e->platform = platform;
    e->surface = surface;
    e->last_ms = platform.now_ms(platform.ctx);
    e->running = true;
    for (unsigned i = 0; i < JELLI_SHAPE_COUNT; ++i)
        e->colors[i].value = palettes[0][i];
    return true;
}

bool jelli_frame(JelliEngine *e)
{
    if (!e->running)
        return false;
    uint64_t now = e->platform.now_ms(e->platform.ctx);
    uint64_t elapsed = now >= e->last_ms ? now - e->last_ms : 0;
    e->last_ms = now;
    if (!e->paused)
        e->animation_ms = (e->animation_ms + (uint32_t)(elapsed % 4000u)) % 4000u;
    for (unsigned i = 0; i < JELLI_SHAPE_COUNT; ++i)
        jelli_color_advance(&e->colors[i], elapsed);
    JelliInput input;
    /* Bound event draining so a busy input source cannot starve rendering. */
    for (unsigned n = 0; e->platform.poll && n < 32 && e->platform.poll(e->platform.ctx, &input);
         ++n) {
        if (input.kind == JELLI_QUIT) {
            e->running = false;
            return false;
        }
        if (input.kind == JELLI_TAP) {
            if (visible_tap(input))
                change_colors(e);
            continue;
        }
        if (input.kind != JELLI_TOGGLE_PAUSE)
            continue;
        e->paused = !e->paused;
        if (e->platform.paused)
            e->platform.paused(e->platform.ctx, e->paused);
    }
    jelli_render(e);
    e->platform.present(e->platform.ctx, &e->surface);
    return true;
}

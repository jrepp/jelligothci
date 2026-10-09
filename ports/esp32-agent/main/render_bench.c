#include "render_bench.h"
#include "screen.h"
#include "jelli/agent_scene.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 466 x 466 panel; the unicorn's 32 x 32 logical art at scale 12 is 384 px. */
enum {
    SIDE = 466,
    SCALE = 12,
    SPRITE = 32 * SCALE,
    ORIGIN = (SIDE - SPRITE) / 2,
    BOB_PX = 6,
    POSE_A = 1041, /* unicorn-idle-a */
    POSE_B = 1042  /* unicorn-idle-b */
};

typedef struct {
    unsigned frames, max_frame_us;
    uint64_t render_us, present_us, pixels;
} Totals;

/* One PSRAM frame (434,312 bytes), kept after the first bench. */
static uint16_t *frame;

static JelliSurface surface(void)
{
    return (JelliSurface){frame, SIDE, SIDE, SIDE, {0u, 0u, 0u, 0u}};
}

static bool ready(void)
{
    if (!frame)
        frame = heap_caps_calloc((size_t)SIDE * SIDE, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    return frame && jelli_asset_find(POSE_A) && jelli_asset_find(POSE_B);
}

/* Draws one frame of the chosen mode; the rectangle to present goes into damage. */
static bool draw(const char *mode, unsigned index, int64_t now_us, int *last_y, JelliRect *damage)
{
    JelliSurface s = surface();
    bool patch = strcmp(mode, "full") != 0 && strcmp(mode, "bob") != 0;
    const JelliAsset *pose = jelli_asset_find(patch && index % 2u ? POSE_B : POSE_A);
    JelliRect all = {0u, 0u, SIDE, SIDE};
    if (!strcmp(mode, "full")) {
        *damage = all;
        return jelli_agent_scene_draw(&s, pose, ORIGIN, ORIGIN, SCALE, all);
    }
    if (!strcmp(mode, "bob")) {
        /* One gentle bob per second, from the clock rather than the frame count. */
        int y = ORIGIN + (int)lrintf(BOB_PX * sinf(6.2831853f * (float)(now_us % 1000000) / 1e6f));
        *damage = jelli_agent_rect_union(jelli_agent_sprite_rect(&s, pose, ORIGIN, *last_y, SCALE),
                                         jelli_agent_sprite_rect(&s, pose, ORIGIN, y, SCALE));
        *last_y = y;
        return jelli_agent_scene_draw(&s, pose, ORIGIN, y, SCALE, *damage);
    }
    /* patch: a 192 x 96 head region, alternating poses as a blink would. */
    *damage = (JelliRect){ORIGIN + 96u, ORIGIN + 48u, 192u, 96u};
    return jelli_agent_scene_draw(&s, pose, ORIGIN, ORIGIN, SCALE, *damage);
}

static void report(const char *mode, const Totals *t, uint64_t elapsed_us, char *out,
                   size_t capacity)
{
    unsigned frames = t->frames ? t->frames : 1u;
    (void)snprintf(
        out, capacity,
        "{\"ok\":true,\"mode\":\"%s\",\"frames\":%u,\"fps_x10\":%" PRIu64 ",\"render_us\":%" PRIu64
        ",\"present_us\":%" PRIu64 ",\"max_frame_us\":%u,\"pixels_per_frame\":%" PRIu64 "}",
        mode, t->frames, elapsed_us ? (uint64_t)t->frames * 10000000u / elapsed_us : 0u,
        t->render_us / frames, t->present_us / frames, t->max_frame_us, t->pixels / frames);
}

static void bench(const char *mode, uint32_t seconds, char *out, size_t capacity)
{
    JelliSurface s = surface();
    JelliRect all = {0u, 0u, SIDE, SIDE};
    int last_y = ORIGIN;
    /* Start from the whole unicorn on screen, as an animation would. */
    if (!jelli_agent_scene_draw(&s, jelli_asset_find(POSE_A), ORIGIN, ORIGIN, SCALE, all) ||
        !jelli_agent_screen_present(&s, all)) {
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"display\"}");
        return;
    }
    Totals t = {0};
    int64_t start = esp_timer_get_time(), end = start + (int64_t)seconds * 1000000;
    for (int64_t now = start; now < end; now = esp_timer_get_time()) {
        JelliRect damage;
        bool drawn = draw(mode, t.frames, now - start, &last_y, &damage);
        int64_t rendered = esp_timer_get_time();
        if (!drawn || !jelli_agent_screen_present(&s, damage)) {
            (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"frame\",\"frames\":%u}",
                           t.frames);
            return;
        }
        int64_t presented = esp_timer_get_time();
        t.render_us += (uint64_t)(rendered - now);
        t.present_us += (uint64_t)(presented - rendered);
        t.pixels += (uint64_t)damage.width * damage.height;
        if ((unsigned)(presented - now) > t.max_frame_us)
            t.max_frame_us = (unsigned)(presented - now);
        ++t.frames;
        if (t.frames % 8u == 0u)
            vTaskDelay(1); /* Let lower-priority tasks and the idle watchdog run. */
    }
    report(mode, &t, (uint64_t)(esp_timer_get_time() - start), out, capacity);
}

bool jelli_agent_command_render(const JelliAgentRequest *request, char *out, size_t capacity)
{
    uint32_t seconds = 0u;
    if (strcmp(request->words[0], "render") != 0)
        return false;
    const char *mode = request->count == 4u ? request->words[2] : "";
    bool known = !strcmp(mode, "full") || !strcmp(mode, "bob") || !strcmp(mode, "patch");
    if (request->count != 4u || strcmp(request->words[1], "bench") != 0 || !known ||
        !jelli_agent_number(request->words[3], &seconds) || !seconds || seconds > 10u)
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"syntax\"}");
    else if (!ready())
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"unavailable\"}");
    else
        bench(mode, seconds, out, capacity);
    return true;
}

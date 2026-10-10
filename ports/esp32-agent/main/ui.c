#include "ui.h"
#include "screen.h"
#include "ui_art.h"
#include "ui_controls.h"
#include "jelli/agent_scene.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum {
    SIDE = JELLI_UI_SIDE,
    FLOAT_PX = 4,       /* Up and down from the centre, at home. */
    FLOAT_US = 4000000, /* One float, up and back. */
    BREATH_US = 900000, /* Each idle pose. */
    TICK_MS = 40,
    FIRST_WANDER_S = 60, /* After start-up. */
    WANDER_S = 150,      /* Between wanders, plus up to WANDER_JITTER_S. */
    WANDER_JITTER_S = 60,
    NONE = -1
};

static const char *TAG = "agent_ui";
static atomic_bool held, redraw;
static atomic_int requested = NONE; /* A routine asked for by the console or the bridge. */
/* Made by jelli_agent_ui_prepare, then the UI task's only. */
static uint16_t *canvas;
static JelliUiArt art;
static bool prepared;
static atomic_bool started; /* The UI task runs, so requests are read. */

/* What is on the panel: the unicorn at home (pose, float) or prancing (pose). */
typedef struct {
    bool prancing;
    unsigned home_pose;
    int home_y;
    JelliPrancePose prance;
} Shown;

static JelliSurface surface(void)
{
    return (JelliSurface){canvas, SIDE, SIDE, SIDE, {0u, 0u, 0u, 0u}};
}

/* Where the shown frame's sprite goes, and which sprite. */
static const JelliAgentSprite *placed(const Shown *shown, int *x, int *y)
{
    if (!shown->prancing) {
        const JelliAgentSprite *sprite = &art.home[shown->home_pose];
        *x = SIDE / 2 - (int)sprite->width / 2;
        *y = SIDE / 2 - (int)sprite->height / 2 + shown->home_y;
        return sprite;
    }
    const JelliPrancePose *pose = &shown->prance;
    int left = SIDE / 2 + pose->x - JELLI_AGENT_PRANCE_FRAME_PX / 2;
    int top = SIDE / 2 + pose->y - JELLI_AGENT_PRANCE_FRAME_PX / 2;
    *x = left + (pose->mirror ? art.mirror_x : art.x);
    *y = top + art.y;
    return &art.prance[pose->frame];
}

/* Draws now over what was shown, and presents the rectangle both cover; the whole
 * screen when all is set. */
static void show(const Shown *now, const Shown *was, bool all)
{
    JelliSurface s = surface();
    int x = 0, y = 0, old_x = 0, old_y = 0;
    const JelliAgentSprite *sprite = placed(now, &x, &y);
    const JelliAgentSprite *old = placed(was, &old_x, &old_y);
    JelliRect damage = all ? (JelliRect){0u, 0u, SIDE, SIDE}
                           : jelli_agent_rect_union(jelli_agent_sprite_area(&s, old, old_x, old_y),
                                                    jelli_agent_sprite_area(&s, sprite, x, y));
    jelli_agent_sprite_draw(&s, sprite, x, y, now->prancing && now->prance.mirror, damage);
    if (!jelli_agent_screen_present(&s, damage))
        ESP_LOGW(TAG, "The panel did not take a frame");
}

static Shown at_home(int64_t now)
{
    float phase = 6.2831853f * (float)(now % FLOAT_US) / (float)FLOAT_US;
    return (Shown){false,
                   (unsigned)(now / BREATH_US % 2),
                   (int)lrintf((float)FLOAT_PX * sinf(phase)),
                   {JELLI_PRANCE_STAND, 0, 0, false}};
}

static bool same(const Shown *a, const Shown *b)
{
    if (a->prancing != b->prancing)
        return false;
    if (!a->prancing)
        return a->home_pose == b->home_pose && a->home_y == b->home_y;
    return a->prance.frame == b->prance.frame && a->prance.x == b->prance.x &&
           a->prance.y == b->prance.y && a->prance.mirror == b->prance.mirror;
}

static int64_t next_wander(int64_t now)
{
    return now + ((int64_t)WANDER_S + (int64_t)(esp_random() % WANDER_JITTER_S)) * 1000000;
}

/* Home most of the time; a routine when asked for or when the wander is due. */
static void ui_task(void *unused)
{
    (void)unused;
    int64_t now = esp_timer_get_time(), wander_at = now + (int64_t)FIRST_WANDER_S * 1000000;
    int64_t began = 0;
    JelliPranceRoutine routine = JELLI_PRANCE_WANDER;
    Shown shown = at_home(now), next;
    show(&shown, &shown, true);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        if (atomic_load(&held))
            continue;
        now = esp_timer_get_time();
        JelliSurface s = surface();
        bool covered = false;
        /* Settings or a dark screen: the unicorn waits, and is redrawn whole after. */
        if (jelli_agent_ui_controls_tick(&s, (uint64_t)now / 1000u, &covered) != JELLI_UI_UNICORN)
            continue;
        bool all = atomic_exchange(&redraw, false) || covered;
        int asked = atomic_exchange(&requested, NONE);
        if (asked != NONE || (!shown.prancing && now >= wander_at)) {
            routine = asked != NONE ? (JelliPranceRoutine)asked : JELLI_PRANCE_WANDER;
            began = now;
            wander_at = next_wander(now);
            all = all || !shown.prancing; /* From full size to prancing size. */
        }
        next = at_home(now);
        if (shown.prancing || began == now) {
            next.prancing =
                jelli_agent_prance_at(routine, (uint32_t)((now - began) / 1000), &next.prance);
            all = all || !next.prancing; /* Back to full size. */
        }
        if (all || !same(&next, &shown)) {
            show(&next, &shown, all);
            shown = next;
        }
    }
}

bool jelli_agent_ui_prepare(void)
{
    canvas = heap_caps_calloc((size_t)SIDE * SIDE, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    prepared = canvas && jelli_agent_ui_art_prepare(&art);
    /* Touch, the side button, and Settings' errands; the unicorn runs without them. */
    if (prepared && !jelli_agent_ui_controls_start())
        ESP_LOGW(TAG, "Touch, the side button, or Settings is unavailable");
    if (!prepared) {
        heap_caps_free(canvas);
        canvas = NULL;
        ESP_LOGE(TAG, "No unicorn: its art or PSRAM is missing; the identity screen stays");
    }
    return prepared;
}

bool jelli_agent_ui_start(void)
{
    if (!prepared)
        return false;
    static StaticTask_t task_control;
    static StackType_t task_stack[4096u / sizeof(StackType_t)];
    atomic_store(&started,
                 xTaskCreateStaticPinnedToCore(ui_task, "agent_ui", sizeof(task_stack), NULL, 3u,
                                               task_stack, &task_control, 1) != NULL);
    return atomic_load(&started);
}

void jelli_agent_ui_hold(bool hold)
{
    atomic_store(&held, hold);
    if (!hold)
        atomic_store(&redraw, true);
}

void jelli_agent_ui_state(JelliLokiState state)
{
    jelli_agent_ui_controls_state(state);
    if (state == JELLI_LOKI_STATE_SUCCESS)
        atomic_store(&requested, JELLI_PRANCE_CELEBRATE);
}

bool jelli_agent_command_ui(const JelliAgentRequest *request, char *out, size_t capacity)
{
    if (strcmp(request->words[0], "ui") != 0)
        return false;
    bool prance = request->count == 3u && !strcmp(request->words[1], "prance");
    const char *which = prance ? request->words[2] : "";
    int routine = !strcmp(which, "wander")      ? JELLI_PRANCE_WANDER
                  : !strcmp(which, "celebrate") ? JELLI_PRANCE_CELEBRATE
                                                : NONE;
    if (routine == NONE)
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"syntax\"}");
    else if (!atomic_load(&started))
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"unavailable\"}");
    else {
        atomic_store(&requested, routine);
        (void)snprintf(out, capacity, "{\"ok\":true,\"ms\":%u}",
                       (unsigned)jelli_agent_prance_length((JelliPranceRoutine)routine));
    }
    return true;
}

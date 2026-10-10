#include "ui_controls.h"
#include "input.h"
#include "prefs_wifi.h"
#include "screen.h"
#include "wifi.h"
#include "jelli/agent_draw.h"
#include "jelli/agent_prefs.h"
#include "jelli/agent_touch.h"
#include "jelli/pet_screen.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Touches, presses, and screen changes the console asks for, as the UI task's input. */
typedef enum { ASK_TAP, ASK_DRAG, ASK_BUTTON, ASK_SLEEP, ASK_WAKE } AskKind;
typedef struct {
    uint8_t kind;
    int x, y;
} Ask;

enum { ASKS = 8 };

static const char *TAG = "agent_controls";
static QueueHandle_t asks;
static StaticQueue_t asks_control;
static uint8_t asks_storage[ASKS * sizeof(Ask)];
static atomic_bool wake_asked, dark_now, started;
/* What Settings shows (JelliPrefsScreen) and how many networks it lists, for status. */
static atomic_int view_now, networks_now;
/* The UI task's alone from here on. */
static JelliPetScreen power;
static JelliAgentTouch touch;
static JelliPrefs prefs;
static JelliPrefsJoin join;      /* A join Settings asked for, until handed over. */
static JelliPrefsAction pending; /* A scan or join still to hand over (errand busy). */
static bool fresh_unicorn;

static void wipe_join(void)
{
    volatile uint8_t *bytes = (volatile uint8_t *)&join;
    for (size_t i = 0u; i < sizeof(join); ++i)
        bytes[i] = 0u;
}

bool jelli_agent_ui_controls_start(void)
{
    asks = xQueueCreateStatic(ASKS, sizeof(Ask), asks_storage, &asks_control);
    jelli_pet_screen_init_idle(&power, 0u, JELLI_AGENT_SCREEN_IDLE_MS);
    jelli_agent_touch_init(&touch);
    jelli_prefs_init(&prefs);
    bool input = jelli_agent_input_start(), errands = jelli_agent_prefs_wifi_start();
    atomic_store(&started, asks != NULL);
    return asks && input && errands;
}

static void open_settings(uint64_t now_ms)
{
    uint8_t ssid[JELLI_PREFS_SSID_MAX];
    size_t size = 0u;
    bool secured = false, connected = false;
    jelli_agent_wifi_current(ssid, &size, &secured, &connected);
    jelli_prefs_open(&prefs, ssid, size, connected, now_ms);
    jelli_prefs_secured(&prefs, secured);
}

static void close_settings(void)
{
    jelli_prefs_close(&prefs);
    pending = JELLI_PREFS_DO_NOTHING;
    wipe_join();
    fresh_unicorn = true;
}

static void wake(uint64_t now_ms)
{
    if (power.asleep)
        jelli_pet_screen_set(&power, false, now_ms);
    jelli_pet_screen_activity(&power, now_ms);
}

/* A scan or join goes to the errand task now, or on a later tick if it is busy. */
static void hand_over(void)
{
    bool handed = pending == JELLI_PREFS_DO_SCAN   ? jelli_agent_prefs_wifi_scan()
                  : pending == JELLI_PREFS_DO_JOIN ? jelli_agent_prefs_wifi_join(&join)
                                                   : true;
    if (!handed)
        return;
    if (pending == JELLI_PREFS_DO_JOIN)
        wipe_join();
    pending = JELLI_PREFS_DO_NOTHING;
}

static void tap(int x, int y, uint64_t now_ms)
{
    if (power.asleep) {
        if (jelli_pet_screen_wake_tap(&power, x, y, now_ms))
            ESP_LOGI(TAG, "Screen awake (double tap)");
        return;
    }
    jelli_pet_screen_activity(&power, now_ms);
    /* The third tap of a triple tap presses nothing under it. */
    if (prefs.screen == JELLI_PREFS_CLOSED || jelli_pet_screen_guarded(&power, now_ms))
        return;
    JelliPrefsAction action = jelli_prefs_tap(&prefs, x, y, now_ms, &join);
    if (action == JELLI_PREFS_DO_CLOSE)
        close_settings();
    else if (action != JELLI_PREFS_DO_NOTHING)
        pending = action;
}

static void drag(int dy, uint64_t now_ms)
{
    if (power.asleep) {
        jelli_pet_screen_cancel_tap(&power);
        return;
    }
    jelli_pet_screen_activity(&power, now_ms);
    jelli_prefs_drag(&prefs, dy);
}

/* The side button wakes a dark screen, else opens or closes Settings. */
static void button(uint64_t now_ms)
{
    if (power.asleep) {
        wake(now_ms);
        return;
    }
    jelli_pet_screen_activity(&power, now_ms);
    if (prefs.screen == JELLI_PREFS_CLOSED)
        open_settings(now_ms);
    else
        close_settings();
}

static void apply(const Ask *ask, uint64_t now_ms)
{
    switch ((AskKind)ask->kind) {
    case ASK_TAP:
        tap(ask->x, ask->y, now_ms);
        break;
    case ASK_DRAG:
        drag(ask->y, now_ms);
        break;
    case ASK_BUTTON:
        button(now_ms);
        break;
    case ASK_SLEEP:
        jelli_pet_screen_set(&power, true, now_ms);
        break;
    case ASK_WAKE:
        wake(now_ms);
        break;
    }
}

static void read_input(uint64_t now_ms)
{
    if (jelli_agent_input_button())
        button(now_ms);
    bool pressed = false;
    int x = 0, y = 0;
    if (jelli_agent_input_touch(&pressed, &x, &y)) {
        if (pressed && !power.asleep)
            jelli_pet_screen_activity(&power, now_ms); /* A finger on the glass keeps it lit. */
        JelliAgentTouchEvent event = jelli_agent_touch_sample(&touch, pressed, x, y, now_ms);
        if (event.kind == JELLI_AGENT_TOUCH_TAP)
            tap(event.x, event.y, now_ms);
        else if (event.kind == JELLI_AGENT_TOUCH_DRAG)
            drag(event.dy, now_ms);
    }
    Ask ask;
    while (asks && xQueueReceive(asks, &ask, 0) == pdTRUE)
        apply(&ask, now_ms);
    if (atomic_exchange(&wake_asked, false))
        wake(now_ms);
}

/* Black, presented, then the light off: nothing shows, at any level. Settings closes,
 * so no password waits on a dark screen. */
static void go_dark(JelliSurface *canvas)
{
    if (prefs.screen != JELLI_PREFS_CLOSED)
        close_settings();
    jelli_agent_fill(canvas, 0, 0, (int)canvas->width, (int)canvas->height, 0u);
    (void)jelli_agent_screen_present(canvas, (JelliRect){0u, 0u, canvas->width, canvas->height});
    if (!jelli_agent_screen_light(false))
        ESP_LOGW(TAG, "The panel's light did not go off");
    ESP_LOGI(TAG, "Screen asleep; a double tap or the side button wakes it");
}

static void run_settings(JelliSurface *canvas, uint64_t now_ms)
{
    jelli_agent_prefs_wifi_deliver(&prefs, now_ms);
    hand_over();
    if (jelli_prefs_tick(&prefs, now_ms)) {
        jelli_prefs_draw(&prefs, canvas);
        if (!jelli_agent_screen_present(canvas, canvas->damage))
            ESP_LOGW(TAG, "The panel did not take Settings");
    }
}

JelliUiView jelli_agent_ui_controls_tick(JelliSurface *canvas, uint64_t now_ms, bool *fresh)
{
    bool was_dark = power.asleep;
    read_input(now_ms);
    if (!power.asleep)
        (void)jelli_pet_screen_idle(&power, now_ms);
    if (!was_dark && power.asleep) /* Idle, or the console asked. */
        go_dark(canvas);
    if (was_dark && !power.asleep) {
        fresh_unicorn = true;
        (void)jelli_agent_screen_light(true);
        ESP_LOGI(TAG, "Screen awake");
    }
    atomic_store(&dark_now, power.asleep);
    atomic_store(&view_now, (int)prefs.screen);
    atomic_store(&networks_now, (int)prefs.count);
    if (power.asleep)
        return JELLI_UI_DARK;
    if (prefs.screen != JELLI_PREFS_CLOSED) {
        run_settings(canvas, now_ms);
        return JELLI_UI_SETTINGS;
    }
    *fresh = fresh_unicorn;
    fresh_unicorn = false;
    return JELLI_UI_UNICORN;
}

void jelli_agent_ui_controls_state(JelliLokiState state)
{
    if (state == JELLI_LOKI_STATE_LISTENING || state == JELLI_LOKI_STATE_ASKING ||
        state == JELLI_LOKI_STATE_SPEAKING)
        atomic_store(&wake_asked, true);
}

static bool send(AskKind kind, int x, int y)
{
    Ask ask = {(uint8_t)kind, x, y};
    return atomic_load(&started) && xQueueSend(asks, &ask, 0) == pdTRUE;
}

static const char *view_name(int view)
{
    static const char *const names[] = {"closed",   "menu",    "searching", "networks",
                                        "password", "joining", "result"};
    return view >= 0 && view < (int)(sizeof(names) / sizeof(names[0])) ? names[view] : "closed";
}

static bool screen_command(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *what = request->count == 2u ? request->words[1] : "";
    bool ok = !strcmp(what, "status") || (!strcmp(what, "sleep") && send(ASK_SLEEP, 0, 0)) ||
              (!strcmp(what, "wake") && send(ASK_WAKE, 0, 0));
    if (!ok)
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"%s\"}",
                       atomic_load(&started) ? "syntax" : "unavailable");
    else
        (void)snprintf(out, capacity,
                       "{\"ok\":true,\"asleep\":%s,\"settings\":\"%s\",\"networks\":%d,"
                       "\"idle_ms\":%u}",
                       atomic_load(&dark_now) ? "true" : "false", view_name(atomic_load(&view_now)),
                       atomic_load(&networks_now), (unsigned)JELLI_AGENT_SCREEN_IDLE_MS);
    return true;
}

bool jelli_agent_command_controls(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *verb = request->words[0], *what = request->count >= 2u ? request->words[1] : "";
    if (!strcmp(verb, "screen"))
        return screen_command(request, out, capacity);
    if (strcmp(verb, "ui") != 0)
        return false;
    uint32_t x = 0u, y = 0u;
    bool sent;
    if (request->count == 2u && !strcmp(what, "button"))
        sent = send(ASK_BUTTON, 0, 0);
    else if (request->count == 4u && !strcmp(what, "tap") &&
             jelli_agent_number(request->words[2], &x) &&
             jelli_agent_number(request->words[3], &y) && x < 466u && y < 466u)
        sent = send(ASK_TAP, (int)x, (int)y);
    else if (request->count == 4u && !strcmp(what, "drag") &&
             jelli_agent_number(request->words[3], &y) && y <= 466u &&
             (!strcmp(request->words[2], "up") || !strcmp(request->words[2], "down")))
        sent = send(ASK_DRAG, 0, !strcmp(request->words[2], "up") ? -(int)y : (int)y);
    else
        return false; /* Other "ui" verbs: the unicorn's own commands. */
    (void)snprintf(out, capacity, sent ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"busy\"}");
    return true;
}

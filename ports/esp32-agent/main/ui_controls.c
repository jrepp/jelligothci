#include "ui_controls.h"
#include "ui_controls_internal.h"
#include "input.h"
#include "loki_link.h"
#include "loki_models.h"
#include "prefs_wifi.h"
#include "screen.h"
#include "voice.h"
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

enum {
    ASKS = 8,
    PET_PATH = 120,   /* Pixels a stroke travels to count as petting (ADR-017). */
    LOST_READS = 3,   /* Failed touch reads in a row that end a hold (about 120 ms). */
    REFUSED_MS = 900, /* The refusal's two blinks. */
    BLINK_MS = 225
};

static const char *TAG = "agent_controls";
static QueueHandle_t asks;
static StaticQueue_t asks_control;
static uint8_t asks_storage[ASKS * sizeof(Ask)];
static atomic_bool wake_asked, dark_now, started;
/* What Settings shows (JelliPrefsScreen), how many networks and models it lists, for
 * status. */
static atomic_int view_now, networks_now, models_now;
/* The UI task's alone from here on. */
static JelliPetScreen power;
static JelliAgentTouch touch;
static JelliPrefs prefs;
static JelliPrefsJoin join;      /* A join Settings asked for, until handed over. */
static JelliPrefsAction pending; /* A scan or join still to hand over (errand busy). */
static JelliPrefsModels offered; /* The bridge's latest list, as last copied. */
static uint32_t models_seen;     /* Its generation. */
static bool online_seen;         /* Whether the bridge was online then. */
static bool fresh_unicorn, petted, refused;
/* Whether the press under way began on the unicorn's lit screen, the only place a press
 * may become a hold (ADR-017); decided when the finger lands, kept until it lifts. */
static bool press_may_hold;
static unsigned lost_reads;
static uint64_t refused_at;

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

/* Settings shows the bridge's latest list of models and whether the bridge is online
 * (ADR-018): copied when either changed, or always when forced. */
static void refresh_models(bool force)
{
    uint32_t generation = jelli_agent_models_generation();
    bool online = jelli_agent_link_online();
    if (!force && generation == models_seen && online == online_seen)
        return;
    models_seen = jelli_agent_models_get(&offered);
    online_seen = online;
    (void)jelli_prefs_models(&prefs, &offered, online);
}

/* The model just picked goes to the bridge now, whose next list confirms it. Sent from
 * this task: the send waits at most the link's send timeout. When it cannot be sent,
 * Settings goes back to what the bridge last said. */
static void send_model(void)
{
    JelliLokiOut choice = {.type = JELLI_LOKI_SET_MODEL, .model = jelli_prefs_model(&prefs)};
    if (choice.model && jelli_agent_link_send(&choice)) {
        ESP_LOGI(TAG, "Asked the bridge to answer with %s", choice.model);
        return;
    }
    ESP_LOGW(TAG, "The model picked could not be sent");
    refresh_models(true);
}

/* Settings and a dark screen show no listening ring, so an utterance ends with either
 * (ADR-017): speech never reaches the bridge unseen. */
static void open_settings(uint64_t now_ms)
{
    jelli_agent_voice_end();
    uint8_t ssid[JELLI_PREFS_SSID_MAX];
    size_t size = 0u;
    bool secured = false, connected = false;
    jelli_agent_wifi_current(ssid, &size, &secured, &connected);
    jelli_prefs_open(&prefs, ssid, size, connected, now_ms);
    jelli_prefs_secured(&prefs, secured);
    refresh_models(true);
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
    else if (action == JELLI_PREFS_DO_SET_MODEL)
        send_model();
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

/* A hold on the unicorn sends what is said to the bridge until it lifts (ADR-017). */
static void hold(uint64_t now_ms)
{
    if (power.asleep || prefs.screen != JELLI_PREFS_CLOSED)
        return;
    jelli_pet_screen_activity(&power, now_ms);
    if (jelli_agent_voice_begin()) {
        ESP_LOGI(TAG, "Listening while held");
        return;
    }
    refused = true;
    refused_at = now_ms;
    ESP_LOGI(TAG, "Not listening: the bridge is offline, or a reply is still open");
}

/* However the hold ended, whatever the screen shows now. */
static void release(uint64_t now_ms)
{
    if (!power.asleep)
        jelli_pet_screen_activity(&power, now_ms);
    jelli_agent_voice_end();
}

/* A stroke long enough, over the unicorn, pets it. */
static void stroke(uint32_t path, uint64_t now_ms)
{
    if (power.asleep) {
        jelli_pet_screen_cancel_tap(&power);
        return;
    }
    jelli_pet_screen_activity(&power, now_ms);
    if (prefs.screen == JELLI_PREFS_CLOSED && path >= PET_PATH)
        petted = true;
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
    case ASK_HOLD:
        hold(now_ms);
        break;
    case ASK_RELEASE:
        release(now_ms);
        break;
    case ASK_PET:
        stroke(PET_PATH, now_ms);
        break;
    }
}

static void read_input(uint64_t now_ms)
{
    if (jelli_agent_input_button())
        button(now_ms);
    bool pressed = false;
    int x = 0, y = 0;
    bool read = jelli_agent_input_touch(&pressed, &x, &y);
    lost_reads = read ? 0u : lost_reads + (lost_reads < LOST_READS);
    /* A controller that stops answering mid-hold must not keep the microphone open:
     * after a few failed reads the finger counts as lifted. */
    if (read || (touch.holding && lost_reads >= LOST_READS)) {
        if (pressed && !power.asleep)
            jelli_pet_screen_activity(&power, now_ms); /* A finger on the glass keeps it lit. */
        /* Holds only on the unicorn: Settings keeps its slow taps, and a press that began
         * in Settings or on a dark screen never becomes one when the view changes. */
        bool unicorn = !power.asleep && prefs.screen == JELLI_PREFS_CLOSED;
        if (!touch.down)
            press_may_hold = unicorn;
        jelli_agent_touch_holds(&touch, unicorn && press_may_hold);
        JelliAgentTouchEvent event = jelli_agent_touch_sample(&touch, pressed, x, y, now_ms);
        switch (event.kind) {
        case JELLI_AGENT_TOUCH_TAP:
            tap(event.x, event.y, now_ms);
            break;
        case JELLI_AGENT_TOUCH_DRAG:
            drag(event.dy, now_ms);
            break;
        case JELLI_AGENT_TOUCH_HOLD:
            hold(now_ms);
            break;
        case JELLI_AGENT_TOUCH_RELEASE:
            release(now_ms);
            break;
        case JELLI_AGENT_TOUCH_STROKE:
            stroke(event.path, now_ms);
            break;
        case JELLI_AGENT_TOUCH_NONE:
            break;
        }
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
    jelli_agent_voice_end();
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
    refresh_models(false);
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
    atomic_store(&models_now, (int)prefs.models.count);
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

JelliListenCue jelli_agent_ui_controls_cue(uint64_t now_ms)
{
    if (jelli_agent_voice_holding())
        return JELLI_CUE_LISTENING;
    if (!refused || now_ms < refused_at || now_ms - refused_at >= REFUSED_MS) {
        refused = false;
        return JELLI_CUE_NONE;
    }
    return (now_ms - refused_at) / BLINK_MS % 2u == 0u ? JELLI_CUE_REFUSED : JELLI_CUE_NONE;
}

bool jelli_agent_ui_controls_petted(void)
{
    bool was = petted;
    petted = false;
    return was;
}

void jelli_agent_ui_controls_state(JelliLokiState state)
{
    if (state == JELLI_LOKI_STATE_LISTENING || state == JELLI_LOKI_STATE_ASKING ||
        state == JELLI_LOKI_STATE_SPEAKING)
        atomic_store(&wake_asked, true);
}

bool jelli_agent_ui_controls_ask(AskKind kind, int x, int y)
{
    Ask ask = {(uint8_t)kind, x, y};
    return atomic_load(&started) && xQueueSend(asks, &ask, 0) == pdTRUE;
}

bool jelli_agent_ui_controls_status(bool *dark, int *view, int *networks, int *models)
{
    *dark = atomic_load(&dark_now);
    *view = atomic_load(&view_now);
    *networks = atomic_load(&networks_now);
    *models = atomic_load(&models_now);
    return atomic_load(&started);
}

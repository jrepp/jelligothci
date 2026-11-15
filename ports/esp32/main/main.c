#include "jelli/sound.h"
#include "jelli/gesture.h"
#include "jelli/pet_engine.h"
#include "debug_wire.h"
#include "sound_output.h"
#include "session.h"
#include "rtc_clock.h"
#include "touch_input.h"
#include "case_button.h"
#include "battery_monitor.h"
#include "auto_sleep.h"
#include "auto_sleep_policy.h"
#include "network.h"
#include "validation/experiments.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "display_output.h"
#include "esp_lv_adapter.h"
#include "pipeline_profile.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>

enum { FRAME_MS = 16 };
enum { TOUCH_TRACE_CAPACITY = 8 };

typedef struct {
    JelliInput input;
    uint32_t press_irq_us, press_event_us;
    uint32_t release_irq_us, release_event_us;
} BoardInput;

typedef struct {
    uint32_t press_irq_us, press_event_us;
    uint32_t release_irq_us, release_event_us;
    uint32_t engine_poll_us;
} TouchTrace;

typedef struct {
    JelliGesture gesture;
    JelliCaseButton button;
    JelliAutoSleep sleep;
    uint64_t last_activity_ms, sleep_retry_ms;
    uint32_t sleep_touch_irq;
    bool prior_blank;
    JelliTouchGuard touch;
    JelliDisplayOutput display;
    QueueHandle_t input;
    uint64_t present_ms;
    uint64_t frame_started_us;
    uint32_t press_irq_us, press_event_us;
    uint32_t present_started_us, present_finished_us;
    TouchTrace touch_trace[TOUCH_TRACE_CAPACITY];
    uint8_t touch_trace_count;
} Board;
static const char *TAG = "jelligotchi";

static uint64_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time() / 1000u;
}
static bool poll_input(void *ctx, JelliInput *input)
{
    Board *b = ctx;
    BoardInput queued;
    if (b->button.blank) {
        (void)xQueueReset(b->input);
        return false;
    }
    if (xQueueReceive(b->input, &queued, 0) != pdTRUE)
        return false;
    *input = queued.input;
    b->last_activity_ms = now_ms(NULL);
    if (input->kind == JELLI_POINTER_UP && b->touch_trace_count < TOUCH_TRACE_CAPACITY) {
        b->touch_trace[b->touch_trace_count++] = (TouchTrace){
            .press_irq_us = queued.press_irq_us,
            .press_event_us = queued.press_event_us,
            .release_irq_us = queued.release_irq_us,
            .release_event_us = queued.release_event_us,
            .engine_poll_us = (uint32_t)esp_timer_get_time(),
        };
    }
    return true;
}
static void queue_touch(Board *b, JelliInput input)
{
    BoardInput queued = {.input = input,
                         .press_irq_us = b->press_irq_us,
                         .press_event_us = b->press_event_us,
                         .release_irq_us = jelli_touch_input_irq_timestamp_us(),
                         .release_event_us = (uint32_t)esp_timer_get_time()};
    if (xQueueSend(b->input, &queued, 0) != pdTRUE) {
        /* Losing any pointer edge cancels the whole gesture, including a pending hold. */
        xQueueReset(b->input);
        queued.input = (JelliInput){.kind = JELLI_POINTER_CANCEL};
        (void)xQueueSend(b->input, &queued, 0);
        b->gesture.active = false;
    }
}

static void cancel_touch(void *ctx)
{
    queue_touch(ctx, (JelliInput){.kind = JELLI_POINTER_CANCEL});
}

static void touch_event(lv_event_t *event)
{
    Board *b = lv_event_get_user_data(event);
    lv_indev_t *device = lv_indev_active();
    if (!device)
        return;
    lv_point_t point;
    lv_indev_get_point(device, &point);
    lv_event_code_t code = lv_event_get_code(event);
    if (b->button.blank || jelli_experiments_filter_touch(code) || code == LV_EVENT_PRESS_LOST ||
        (code != LV_EVENT_PRESSED && !b->gesture.active)) {
        b->gesture.active = false;
        queue_touch(b, (JelliInput){.kind = JELLI_POINTER_CANCEL});
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        b->press_irq_us = jelli_touch_input_irq_timestamp_us();
        b->press_event_us = (uint32_t)esp_timer_get_time();
        jelli_gesture_begin(&b->gesture, point.x, point.y);
        queue_touch(b, (JelliInput){JELLI_POINTER_DOWN, point.x, point.y});
    } else if (code == LV_EVENT_PRESSING) {
        queue_touch(b, (JelliInput){JELLI_POINTER_MOVE, point.x, point.y});
    } else if (code == LV_EVENT_RELEASED) {
        b->gesture.active = false;
        queue_touch(b, (JelliInput){JELLI_POINTER_UP, point.x, point.y});
    }
}

static void present(void *ctx, const JelliSurface *surface)
{
    Board *b = ctx;
    jelli_profile_record(JELLI_PROFILE_ENGINE, b->frame_started_us, 0u);
    b->present_ms = 0;
    b->present_started_us = (uint32_t)esp_timer_get_time();
    uint64_t start = now_ms(ctx);
    jelli_display_output_present(&b->display, surface, start);
    b->present_ms = now_ms(ctx) - start;
    b->present_finished_us = (uint32_t)esp_timer_get_time();
}
static void paused(void *ctx, bool value)
{
    (void)ctx;
    ESP_LOGI(TAG, "Animation %s", value ? "paused" : "running");
}

/* The engine owns this state; touch IRQ timestamps cross tasks under the touch lock. */
static void sleep_wake(Board *board, bool battery, bool eligible, bool touch, bool button,
                       uint64_t now)
{
    if (jelli_auto_sleep_should_wake(board->sleep.active, eligible, board->button.blank, touch,
                                     button)) {
        jelli_auto_sleep_leave(&board->sleep, &board->display);
        if (touch || button || !battery)
            (void)jelli_case_button_set_blank(&board->button, false);
        board->last_activity_ms = now;
        board->prior_blank = board->button.blank;
    } else if (!board->sleep.active && touch) {
        (void)jelli_case_button_set_blank(&board->button, false);
        board->last_activity_ms = now;
        board->prior_blank = board->button.blank;
    }
}

static void sleep_enter(Board *board, bool eligible, uint64_t now)
{
    if (eligible && !board->button.blank && jelli_auto_sleep_idle(now, board->last_activity_ms) &&
        jelli_case_button_set_blank(&board->button, true)) {
        board->prior_blank = true;
        board->sleep_touch_irq = jelli_touch_input_irq_timestamp_us();
    }
    if (eligible && board->button.blank && !board->sleep.active && now >= board->sleep_retry_ms &&
        !jelli_auto_sleep_enter(&board->sleep, &board->display))
        board->sleep_retry_ms = now + 10000u;
}

static bool sleep_touch_pending(const Board *board, uint32_t irq)
{
    return board->button.blank && (irq != board->sleep_touch_irq ||
                                   (board->sleep.active && gpio_get_level(BSP_LCD_TOUCH_INT) == 0));
}

static bool battery_sleep_poll(JelliPetEngine *engine, Board *board, JelliEspSession *session,
                               uint64_t now, bool button_pressed, bool frozen)
{
    if (frozen)
        return false;
    bool battery = engine->ui.battery_reading_valid && (engine->ui.battery_status[0] & 0x20u) == 0u;
    uint32_t irq = jelli_touch_input_irq_timestamp_us();
    if (board->button.blank != board->prior_blank) {
        board->prior_blank = board->button.blank;
        board->sleep_touch_irq = irq;
        board->last_activity_ms = now;
    }
    bool touch = sleep_touch_pending(board, irq);
    bool eligible = jelli_auto_sleep_allowed(engine->ui.battery_reading_valid,
                                             engine->ui.battery_status[0], board->button.io != NULL,
                                             jelli_network_idle_offline(), engine->game.resuming);
    sleep_wake(board, battery, eligible, touch, button_pressed, now);
    sleep_enter(board, eligible, now);
    if (!board->sleep.active)
        return false;
    uint64_t elapsed = now >= engine->last_ms ? now - engine->last_ms : 0u;
    if (!engine->paused)
        jelli_game_advance(&engine->game, elapsed);
    engine->last_ms = now;
    jelli_esp_session_update(session, engine, false);
    /* PWR is on an I2C expander without a direct wake IRQ; poll promptly. */
    vTaskDelay(pdMS_TO_TICKS(50));
    return true;
}

static void run_engine(JelliPetEngine *engine, Board *board, JelliEspSession *session)
{
    uint64_t report_start = now_ms(NULL);
    unsigned frames = 0;
    board->last_activity_ms = now_ms(NULL);
    for (;;) {
        uint64_t start = now_ms(NULL);
        bool button_pressed = jelli_case_button_poll(&board->button, start, board->sleep.active);
        if (board->button.blank) {
            jelli_pet_pointer(&engine->ui, &engine->game,
                              (JelliInput){.kind = JELLI_POINTER_CANCEL}, start);
            (void)xQueueReset(board->input);
        }
        bool frozen = jelli_debug_wire_poll(engine, start);
        if (!frozen)
            jelli_battery_monitor_poll(engine, board->button.blank, start);
        if (battery_sleep_poll(engine, board, session, start, button_pressed, frozen))
            continue;
        jelli_experiments_poll(&board->display, engine);
        jelli_network_poll(engine, session, frozen);
        jelli_esp_session_update(session, engine, frozen);
        board->frame_started_us = jelli_profile_now();
        board->touch_trace_count = 0u;
        board->present_started_us = 0u;
        board->present_finished_us = 0u;
        if (!frozen && !jelli_pet_frame(engine))
            return;
        if (!frozen) {
            for (uint8_t i = 0u; i < board->touch_trace_count; ++i) {
                const TouchTrace *trace = &board->touch_trace[i];
                uint32_t press_irq_to_event = trace->press_event_us - trace->press_irq_us;
                uint32_t last_irq_age_at_release = trace->release_event_us - trace->release_irq_us;
                uint32_t release_to_engine = trace->engine_poll_us - trace->release_event_us;
                uint32_t release_to_present =
                    board->present_started_us ? board->present_started_us - trace->release_event_us
                                              : 0u;
                uint32_t present_copy = board->present_started_us
                                            ? board->present_finished_us - board->present_started_us
                                            : 0u;
                bool release_irq_new = trace->release_irq_us != trace->press_irq_us;
                ESP_LOGI(TAG,
                         "Touch latency us: down-IRQ->press=%" PRIu32 " hold=%" PRIu32
                         " last-IRQ-age-at-release=%" PRIu32 " irq-changed=%u "
                         "release->engine=%" PRIu32 " release->present=%" PRIu32
                         " present-copy=%" PRIu32,
                         press_irq_to_event, trace->release_event_us - trace->press_event_us,
                         last_irq_age_at_release, (unsigned)release_irq_new, release_to_engine,
                         release_to_present, present_copy);
            }
        }
        unsigned cue =
            !frozen && !engine->paused
                ? jelli_pet_ui_sound(&engine->ui, &engine->game.pets[engine->game.active], start)
                : 0u;
        if (cue && engine->game.volume)
            (void)jelli_sound_output_request(NULL, cue - 1u,
                                             jelli_sound_volume(cue - 1u, engine->game.volume));
        jelli_esp_session_update(session, engine, frozen);
        if (!frozen)
            ++frames;
        uint64_t end = now_ms(NULL);
        if (end - report_start >= 5000u) {
            ESP_LOGI(TAG, "Engine update rate: %u Hz (target interval %u ms)",
                     (unsigned)((uint64_t)frames * 1000u / (end - report_start)),
                     (unsigned)FRAME_MS);
            ESP_LOGI(TAG, "Frame sample: render=%" PRIu64 " ms, present=%" PRIu64 " ms",
                     end - start - (frozen ? 0u : board->present_ms),
                     frozen ? 0u : board->present_ms);
            frames = 0;
            report_start = end;
        }
        uint64_t elapsed = now_ms(NULL) - start;
        /* Never catch up in a burst; yield at least one tick on an overrun. */
        uint32_t delay_ms = elapsed < FRAME_MS ? FRAME_MS - (uint32_t)elapsed : 1u;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void app_main(void)
{
    static Board board;
    static JelliPetEngine engine;
    static JelliEspSession session;
    static JelliRegisterIo rtc_io;
    jelli_display_output_init(&board.display);
    board.input = xQueueCreate(8, sizeof(BoardInput));
    ESP_ERROR_CHECK(board.input ? ESP_OK : ESP_ERR_NO_MEM);
    lv_display_t *display = jelli_display_prepare();
    ESP_ERROR_CHECK(display ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    ESP_ERROR_CHECK(
        jelli_touch_input_init(&board.touch, &board.gesture, bsp_display_get_input_dev()));
    board.touch.cancel = cancel_touch;
    board.touch.cancel_ctx = &board;
    jelli_display_transfer_attach(&board.display.transfer, display);
    lv_timer_set_period(lv_display_get_refr_timer(display), FRAME_MS);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    board.display.canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(board.display.canvas, board.display.canvas_pixels, JELLI_WIDTH,
                         JELLI_HEIGHT, LV_COLOR_FORMAT_RGB565);
    const lv_draw_buf_t *canvas_buffer = lv_canvas_get_draw_buf(board.display.canvas);
    ESP_ERROR_CHECK(canvas_buffer->header.stride == JELLI_WIDTH * sizeof(uint16_t)
                        ? ESP_OK
                        : ESP_ERR_INVALID_SIZE);
    lv_obj_center(board.display.canvas);
    lv_obj_remove_flag(board.display.canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(board.display.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board.display.canvas, touch_event, LV_EVENT_PRESSED, &board);
    lv_obj_add_event_cb(board.display.canvas, touch_event, LV_EVENT_PRESSING, &board);
    lv_obj_add_event_cb(board.display.canvas, touch_event, LV_EVENT_RELEASED, &board);
    lv_obj_add_event_cb(board.display.canvas, touch_event, LV_EVENT_PRESS_LOST, &board);
    /* LVGL's task owns refresh work and its stack. Wait for the custom draw
     * callback after releasing the mutex, before checked panel commands. */
    lv_obj_invalidate(board.display.canvas);
    bsp_display_unlock();
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_ERROR_CHECK(jelli_display_transfer_wait_ready(&board.display.transfer));
    JelliDisplayDriver display_driver = jelli_display_output_driver(&board.display);
    ESP_ERROR_CHECK(jelli_display_brightness(display_driver, 60));

    jelli_case_button_init(&board.button, &board.display, &board.touch);

    JelliPlatform platform = {&board, now_ms, poll_input, present, paused};
    JelliSurface surface = {.pixels = board.display.engine_pixels,
                            .width = JELLI_WIDTH,
                            .height = JELLI_HEIGHT,
                            .stride = JELLI_WIDTH};
    ESP_ERROR_CHECK(jelli_pet_init(&engine, platform, surface) ? ESP_OK : ESP_FAIL);
    jelli_pet_bind_devices(&engine, (JelliMotionDriver){0}, display_driver);
    JelliClockDriver clock = jelli_rtc_clock_open(&rtc_io, bsp_i2c_get_handle());
    jelli_esp_session_open(&session, &engine, clock);
    if (!jelli_battery_monitor_start(bsp_i2c_get_handle()))
        ESP_LOGW(TAG, "Battery monitor unavailable");
    ESP_LOGI(TAG, "Pet slice ready: tap menus; NVS checkpoint and RTC session initialized");
    if (!jelli_sound_output_init())
        ESP_LOGW(TAG, "Sound unavailable; game remains playable");
    jelli_pet_bind_music(
        &engine, (JelliMusicDriver){NULL, jelli_music_output_control, jelli_music_output_status});
    jelli_network_init(&engine);
    jelli_experiments_init(&board.touch);
    jelli_debug_wire_init(&board.display);
    run_engine(&engine, &board, &session);
}

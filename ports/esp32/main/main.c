#include "jelli/sound.h"
#include "jelli/gesture.h"
#include "jelli/pet_engine.h"
#include "debug_wire.h"
#include "sound_output.h"
#include "session.h"
#include "rtc_clock.h"
#include "touch_input.h"
#include "case_button.h"
#include "network.h"
#include "validation/experiments.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "display_output.h"
#include "esp_lv_adapter.h"
#include "pipeline_profile.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>

enum { FRAME_MS = 16 };

typedef struct {
    JelliGesture gesture;
    JelliCaseButton button;
    JelliTouchGuard touch;
    JelliDisplayOutput display;
    QueueHandle_t input;
    uint64_t present_ms;
    uint64_t frame_started_us;
} Board;
static const char *TAG = "jelligotchi";

static uint64_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time() / 1000u;
}
static bool poll_input(void *ctx, JelliInput *input)
{
    Board *board = ctx;
    if (board->button.blank) {
        /* Only touch events enter this bounded queue. Drop stale gestures. */
        for (unsigned i = 0; i < 8u; ++i)
            if (xQueueReceive(board->input, input, 0) != pdTRUE)
                break;
        return false;
    }
    return xQueueReceive(board->input, input, 0) == pdTRUE;
}
static void queue_touch(Board *board, JelliInput input)
{
    if (xQueueSend(board->input, &input, 0) != pdTRUE) {
        /* Discard stale input and cancel the contact if its end cannot be queued. */
        (void)xQueueReset(board->input);
        board->gesture.active = false;
        input = (JelliInput){.kind = JELLI_PRESS_CANCEL};
        (void)xQueueSend(board->input, &input, 0);
    }
}
static void cancel_touch(void *ctx) { queue_touch(ctx, (JelliInput){.kind = JELLI_PRESS_CANCEL}); }
static void touch_event(lv_event_t *event)
{
    Board *b = lv_event_get_user_data(event);
    lv_indev_t *device = lv_indev_active();
    if (!device)
        return;
    lv_point_t point;
    lv_indev_get_point(device, &point);
    lv_event_code_t code = lv_event_get_code(event);
    if (b->button.blank || jelli_experiments_filter_touch(code)) {
        b->gesture.active = false;
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        jelli_gesture_begin_at(&b->gesture, point.x, point.y, now_ms(b));
        JelliInput input = {JELLI_PRESS_BEGIN, point.x, point.y};
        queue_touch(b, input);
    } else if (code == LV_EVENT_PRESSING && b->gesture.active &&
               (point.x != b->gesture.last_x || point.y != b->gesture.last_y)) {
        (void)jelli_gesture_move(&b->gesture, point.x, point.y);
        JelliInput input = {JELLI_PRESS_MOVE, point.x, point.y};
        queue_touch(b, input);
    } else if (code == LV_EVENT_PRESS_LOST) {
        b->gesture.active = false;
        JelliInput input = {.kind = JELLI_PRESS_CANCEL};
        queue_touch(b, input);
    } else if (code == LV_EVENT_RELEASED) {
        JelliInput input;
        /* The LVGL task produces events; the engine task consumes them. */
        if (!jelli_gesture_end_at(&b->gesture, point.x, point.y, now_ms(b), &input))
            input = (JelliInput){.kind = JELLI_PRESS_CANCEL};
        queue_touch(b, input);
    }
}
static void present(void *ctx, const JelliSurface *surface)
{
    Board *b = ctx;
    jelli_profile_record(JELLI_PROFILE_ENGINE, b->frame_started_us, 0u);
    b->present_ms = 0;
    uint64_t start = now_ms(ctx);
    jelli_display_output_present(&b->display, surface, start);
    b->present_ms = now_ms(ctx) - start;
}
static void paused(void *ctx, bool value)
{
    (void)ctx;
    ESP_LOGI(TAG, "Animation %s", value ? "paused" : "running");
}

static void run_engine(JelliPetEngine *engine, Board *board, JelliEspSession *session)
{
    uint64_t report_start = now_ms(NULL);
    unsigned frames = 0;
    for (;;) {
        uint64_t start = now_ms(NULL);
        jelli_case_button_poll(&board->button, start);
        if (board->button.blank) {
            engine->hold_active = false;
            engine->hold_consumed = true;
            JelliInput discarded;
            for (unsigned i = 0; i < 8u; ++i)
                if (xQueueReceive(board->input, &discarded, 0) != pdTRUE)
                    break;
        }
        bool frozen = jelli_debug_wire_poll(engine, start);
        jelli_experiments_poll(&board->display, engine);
        jelli_network_poll(engine, session, frozen);
        jelli_esp_session_update(session, engine, frozen);
        board->frame_started_us = jelli_profile_now();
        if (!frozen && !jelli_pet_frame(engine))
            return;
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
    board.input = xQueueCreate(8, sizeof(JelliInput));
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

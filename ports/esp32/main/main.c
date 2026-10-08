#include "jelli/gesture.h"
#include "jelli/pet_engine.h"
#include "debug_wire.h"
#include "sound_output.h"
#include "session.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_heap_caps.h"
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
    lv_obj_t *canvas;
    uint16_t *canvas_pixels;
    QueueHandle_t input;
    uint64_t present_ms;
} Board;
static const char *TAG = "jelligotchi";

static uint64_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time() / 1000u;
}
static bool poll_input(void *ctx, JelliInput *input)
{
    return xQueueReceive(((Board *)ctx)->input, input, 0) == pdTRUE;
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
    if (code == LV_EVENT_PRESSED)
        jelli_gesture_begin(&b->gesture, point.x, point.y);
    else if (code == LV_EVENT_PRESS_LOST)
        b->gesture.active = false;
    else if (code == LV_EVENT_RELEASED) {
        JelliInput input;
        /* The LVGL task produces events; the engine task consumes them. */
        if (jelli_gesture_end(&b->gesture, point.x, point.y, &input))
            (void)xQueueSend(b->input, &input, 0);
    }
}
static void present(void *ctx, const JelliSurface *surface)
{
    Board *b = ctx;
    b->present_ms = 0;
    const JelliRect r = surface->damage;
    if (!r.width || !r.height)
        return;
    uint64_t start = now_ms(ctx);
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    /* Copy damage into LVGL-owned memory before releasing the mutex. */
    for (unsigned y = r.y; y < r.y + r.height; ++y)
        memcpy(b->canvas_pixels + y * JELLI_WIDTH + r.x,
               surface->pixels + y * surface->stride + r.x, r.width * sizeof(uint16_t));
    lv_area_t area = {(int32_t)r.x, (int32_t)r.y, (int32_t)(r.x + r.width - 1u),
                      (int32_t)(r.y + r.height - 1u)};
    lv_obj_invalidate_area(b->canvas, &area);
    bsp_display_unlock();
    b->present_ms = now_ms(ctx) - start;
}
static void paused(void *ctx, bool value)
{
    (void)ctx;
    ESP_LOGI(TAG, "Animation %s", value ? "paused" : "running");
}

static void run_engine(JelliPetEngine *engine, const Board *board, JelliEspSession *session)
{
    uint64_t report_start = now_ms(NULL);
    unsigned frames = 0;
    for (;;) {
        uint64_t start = now_ms(NULL);
        bool frozen = jelli_debug_wire_poll(engine, start);
        jelli_esp_session_update(session, engine, frozen);
        if (!frozen && !jelli_pet_frame(engine))
            return;
        unsigned cue =
            !frozen && !engine->paused
                ? jelli_pet_ui_sound(&engine->ui, &engine->game.pets[engine->game.active], start)
                : 0u;
        if (cue)
            (void)jelli_sound_output_request(NULL, cue - 1u, cue == 6u ? 25u : 18u);
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
    const size_t bytes = JELLI_WIDTH * JELLI_HEIGHT * sizeof(uint16_t);
    uint16_t *pixels = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    board.canvas_pixels = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    board.input = xQueueCreate(8, sizeof(JelliInput));
    ESP_ERROR_CHECK(pixels && board.canvas_pixels && board.input ? ESP_OK : ESP_ERR_NO_MEM);
    lv_display_t *display = bsp_display_start();
    ESP_ERROR_CHECK(display ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    lv_timer_set_period(lv_display_get_refr_timer(display), FRAME_MS);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    board.canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(board.canvas, board.canvas_pixels, JELLI_WIDTH, JELLI_HEIGHT,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_center(board.canvas);
    lv_obj_remove_flag(board.canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(board.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board.canvas, touch_event, LV_EVENT_PRESSED, &board);
    lv_obj_add_event_cb(board.canvas, touch_event, LV_EVENT_RELEASED, &board);
    lv_obj_add_event_cb(board.canvas, touch_event, LV_EVENT_PRESS_LOST, &board);
    bsp_display_unlock();
    ESP_ERROR_CHECK(bsp_display_brightness_set(60));

    JelliPlatform platform = {&board, now_ms, poll_input, present, paused};
    JelliSurface surface = {
        .pixels = pixels, .width = JELLI_WIDTH, .height = JELLI_HEIGHT, .stride = JELLI_WIDTH};
    ESP_ERROR_CHECK(jelli_pet_init(&engine, platform, surface) ? ESP_OK : ESP_FAIL);
    jelli_esp_session_open(&session, &engine);
    ESP_LOGI(TAG, "Pet slice ready: tap menus; NVS checkpoint and RTC session initialized");
    if (!jelli_sound_output_init())
        ESP_LOGW(TAG, "Sound unavailable; game remains playable");
    jelli_debug_wire_init();
    run_engine(&engine, &board, &session);
}

#include "jelli/engine.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

typedef struct {
    lv_obj_t *canvas;
    uint16_t *canvas_pixels;
    QueueHandle_t input;
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
    JelliInput input = {JELLI_TAP, point.x, point.y};
    /* The LVGL task produces events; the engine task consumes them. */
    (void)xQueueSend(b->input, &input, 0);
}
static void present(void *ctx, const JelliSurface *surface)
{
    Board *b = ctx;
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    /* LVGL owns this second buffer. Never hand its async task engine memory. */
    for (unsigned y = 0; y < surface->height; ++y)
        memcpy(b->canvas_pixels + y * JELLI_WIDTH, surface->pixels + y * surface->stride,
               JELLI_WIDTH * sizeof(uint16_t));
    lv_obj_invalidate(b->canvas);
    bsp_display_unlock();
}
static void paused(void *ctx, bool value)
{
    (void)ctx;
    ESP_LOGI(TAG, "Animation %s", value ? "paused" : "running");
}

void app_main(void)
{
    static Board board;
    static JelliEngine engine;
    const size_t bytes = JELLI_WIDTH * JELLI_HEIGHT * sizeof(uint16_t);
    uint16_t *pixels = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    board.canvas_pixels = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    board.input = xQueueCreate(8, sizeof(JelliInput));
    ESP_ERROR_CHECK(pixels && board.canvas_pixels && board.input ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(bsp_display_start() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    board.canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(board.canvas, board.canvas_pixels, JELLI_WIDTH, JELLI_HEIGHT,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_center(board.canvas);
    lv_obj_add_flag(board.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board.canvas, touch_event, LV_EVENT_PRESSED, &board);
    bsp_display_unlock();
    ESP_ERROR_CHECK(bsp_display_brightness_set(60));

    JelliPlatform platform = {&board, now_ms, poll_input, present, paused};
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH};
    ESP_ERROR_CHECK(jelli_init(&engine, platform, surface) ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "Shapes MVP ready: tap to pause/resume");
    while (jelli_frame(&engine))
        vTaskDelay(pdMS_TO_TICKS(33));
}

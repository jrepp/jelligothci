#include "screen.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_attr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Full-width stripes keep the CO5300's even-start/odd-end window rule (BSP rounder)
 * and need only a small internal DMA buffer instead of a whole frame. */
enum { WIDTH = BSP_LCD_H_RES, HEIGHT = BSP_LCD_V_RES, STRIPE = 16, BRIGHTNESS = 60 };
_Static_assert(STRIPE % 2 == 0 && HEIGHT % 2 == 0 && WIDTH % 2 == 0, "CO5300 windows");

static const char *TAG = "agent_screen";
static SemaphoreHandle_t transferred;
static StaticSemaphore_t transferred_storage;
/* One stripe of internal DMA memory (14,912 bytes), owned by the DMA from each draw
 * until its completion callback; static so a timed-out transfer never touches freed
 * memory. */
static DMA_ATTR uint16_t stripe[WIDTH * STRIPE];

static bool IRAM_ATTR transfer_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *event, void *ctx)
{
    (void)io;
    (void)event;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(transferred, &woken);
    return woken == pdTRUE;
}

/* The panel takes big-endian RGB565; the BSP's LVGL adapter swaps bytes the same way. */
static uint16_t panel_color(unsigned r, unsigned g, unsigned b)
{
    unsigned color = ((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3);
    return (uint16_t)(((color >> 8) | (color << 8)) & 0xffffu);
}

static uint16_t identity_pixel(int x, int y)
{
    static const int outer = 214 * 214, inner = 198 * 198, dot = 18 * 18;
    int dx = x - WIDTH / 2, dy = y - HEIGHT / 2;
    int distance = dx * dx + dy * dy;
    if (distance < outer && distance >= inner)
        return panel_color(0xc8u, 0xa2u, 0xffu); /* Lilac: the agent, not the pet. */
    if (distance < dot)
        return panel_color(0xffu, 0xffu, 0xffu);
    return 0u;
}

static bool draw_identity(esp_lcd_panel_handle_t panel)
{
    for (int y = 0; y < HEIGHT; y += STRIPE) {
        int rows = HEIGHT - y < STRIPE ? HEIGHT - y : STRIPE;
        for (int row = 0; row < rows; ++row)
            for (int x = 0; x < WIDTH; ++x)
                stripe[row * WIDTH + x] = identity_pixel(x, y + row);
        if (esp_lcd_panel_draw_bitmap(panel, 0, y, WIDTH, y + rows, stripe) != ESP_OK ||
            xSemaphoreTake(transferred, pdMS_TO_TICKS(200)) != pdTRUE)
            return false; /* The DMA may still read the stripe: never reuse it early. */
    }
    return true;
}

bool jelli_agent_screen_start(void)
{
    transferred = xSemaphoreCreateBinaryStatic(&transferred_storage);
    const bsp_display_config_t config = {.max_transfer_sz = sizeof(stripe)};
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_callbacks_t callbacks = {.on_color_trans_done = transfer_done};
    bool ok = bsp_display_new(&config, &panel, &io) == ESP_OK &&
              esp_lcd_panel_io_register_event_callbacks(io, &callbacks, NULL) == ESP_OK &&
              draw_identity(panel) && bsp_display_brightness_set(BRIGHTNESS) == ESP_OK;
    ESP_LOGI(TAG, "Identity screen %s", ok ? "shown" : "failed");
    return ok;
}

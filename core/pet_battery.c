#include "pet_battery.h"
#include <string.h>

/* Upper left of the home scene: inside the round panel, clear of every creature
 * frame including the ninja's headband tails on the right, below the "@ HOME"
 * heading (y < 84) and above the status caption scrim (y >= 246). Captions are at
 * most seven glyphs at 2x; the caption scrim spans rows CAPTION_Y-8..CAPTION_Y+31
 * and half-width 8*len+8. tests/test_pet_battery_layout.c checks the creature clearance;
 * the closest frame, happy Lilac, reaches x = 162, four pixels right of the rectangle. */
enum {
    BATTERY_CENTER_X = 94,
    BATTERY_ICON_Y = 96,
    BATTERY_CAPTION_Y = 126,
    BATTERY_HALF = 7 * 8 + 8,
    BATTERY_LEFT = BATTERY_CENTER_X - BATTERY_HALF,
    BATTERY_TOP = BATTERY_ICON_Y,
    BATTERY_WIDTH = 2 * BATTERY_HALF + 1,
    BATTERY_HEIGHT = BATTERY_CAPTION_Y + 32 - BATTERY_ICON_Y
};

void jelli_pet_battery_key(const JelliPetUi *ui, const JelliGame *game, JelliPetRenderKey *key)
{
    key->battery_display = game->battery_display;
    /* Rings, collection, and activities occupy this corner; show it on the resting scene. */
    key->battery_shown = game->battery_display && !ui->menu_open;
    key->battery = key->battery_shown ? jelli_power_normalize(jelli_pet_power(ui))
                                      : (JelliPowerReading){0u, 0u};
}

bool jelli_pet_battery_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return a->battery_display == b->battery_display && a->battery_shown == b->battery_shown &&
           a->battery.state == b->battery.state && a->battery.percent == b->battery.percent;
}

void jelli_pet_battery_copy(JelliPetRenderKey *destination, const JelliPetRenderKey *source)
{
    destination->battery = source->battery;
    destination->battery_shown = source->battery_shown;
    destination->battery_display = source->battery_display;
}

JelliRect jelli_pet_battery_bounds(void)
{
    return (JelliRect){BATTERY_LEFT, BATTERY_TOP, BATTERY_WIDTH, BATTERY_HEIGHT};
}

void jelli_pet_battery_draw(Canvas *canvas, const JelliPetRenderKey *view)
{
    if (!view->battery_shown)
        return;
    JelliBatteryIndicator indicator = jelli_battery_indicator(view->battery);
    /* The scrim keeps the caption legible on the bright daytime scene; drawn first
     * so it never dims the icon. Low charge is spelled out ("LOW 8%") and shown by
     * bar count, not only by color. */
    int half = (int)strlen(indicator.caption) * 8;
    jelli_canvas_scrim(canvas, BATTERY_CENTER_X, BATTERY_CAPTION_Y, half + 8);
    jelli_canvas_sprite(canvas, JELLI_BATTERY_ASSET_BASE + indicator.icon, BATTERY_CENTER_X - 16,
                        BATTERY_ICON_Y, 2u);
    jelli_canvas_text(canvas, indicator.caption, BATTERY_CENTER_X - half, BATTERY_CAPTION_Y, 2u,
                      indicator.low ? PINK : PALE);
}

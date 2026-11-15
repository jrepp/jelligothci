#include "pet_canvas.h"
#include "jelli_asset_ids.h"
#include <stdio.h>

static void meter_sparkle(Canvas *c, int x, int y, int radius, uint16_t color)
{
    jelli_canvas_rect(c, x - 1, y - radius, 3, radius * 2 + 1, color);
    jelli_canvas_rect(c, x - radius, y - 1, radius * 2 + 1, 3, color);
}

void jelli_pet_meter_tile(Canvas *c, const JelliPetRenderKey *v, unsigned index, int x)
{
    static const char *const labels[] = {"MOOD",    "FOOD", "ENERGY", "CLEAN", "FUN",
                                         "FRIENDS", "LOVE", "SLEEP",  "WATER"};
    index %= JELLI_PET_STAT_COUNT;
    unsigned score = v->stat_value > 1000u ? 100u : v->stat_value / 10u;
    jelli_canvas_rect(c, x, 284, 286, 96, INK);
    jelli_canvas_rect(c, x + 4, 288, 278, 88, BG);
    jelli_canvas_rect(c, x + 12, 296, 76, 72, INK);
    static const uint16_t colors[] = {TEAL, MINT, TEAL, MINT, GOLD, GOLD};
    unsigned fill = score * 60u / 100u;
    for (unsigned step = 0u; step < 6u; ++step) {
        unsigned height = fill > step * 10u ? fill - step * 10u : 0u;
        if (height > 10u)
            height = 10u;
        if (height)
            jelli_canvas_rect(c, x + 12, 368 - (int)step * 12 - (int)height, 76, (int)height,
                              score < 25u ? PINK : colors[step]);
    }
    uint32_t icon = index == 8u   ? JELLI_ASSET_MENUS_WATER
                    : index == 7u ? JELLI_ASSET_ICONS_REST
                    : index == 6u ? JELLI_ASSET_EFFECTS_HEART
                    : !index      ? JELLI_ASSET_METERS_SOCIAL
                                  : JELLI_ASSET_METER_BASE + index;
    jelli_canvas_sprite(c, icon, x + 18, 300, 2u);
    char number[4];
    int size = snprintf(number, sizeof(number), "%u", score);
    if (size > 0 && (size_t)size < sizeof(number))
        jelli_canvas_text(c, number, x + 112, 287, 4u, PALE);
    jelli_canvas_text(c, labels[index], x + 112, 344, 2u, MINT);
    uint16_t sparkle = score < 25u ? PINK : GOLD;
    meter_sparkle(c, x + 224, 307, v->reward_active && v->tile_phase ? 7 : 4, sparkle);
    meter_sparkle(c, x + 252, 325, v->reward_active && !v->tile_phase ? 6 : 3, MINT);
}

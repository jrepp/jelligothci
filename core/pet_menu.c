#include "pet_menu.h"
#include "jelli/nutrition.h"
#include <string.h>

#define RING_ICON_PX 32u /* Ring item icons draw at about this size. */

static void label_line(Canvas *c, const char *text, int x, int y)
{
    int width = (int)strlen(text) * 16;
    int left = x - width / 2;
    if (left < 18)
        left = 18;
    if (left + width > 448)
        left = 448 - width;
    jelli_canvas_rect(c, left - 2, y - 1, width + 4, 26, BG);
    jelli_canvas_text(c, text, left, y, 2u, PALE);
}

static void button_label(Canvas *c, const char *label, int x, int y)
{
    const char *space = strchr(label, ' ');
    if (strlen(label) > 8u && space && space - label <= 8) {
        char first[9] = {0};
        memcpy(first, label, (size_t)(space - label));
        label_line(c, first, x, y - 12);
        label_line(c, space + 1, x, y + 12);
    } else {
        label_line(c, label, x, y);
    }
}

static void start_badge(Canvas *c, int x, int y)
{
    jelli_canvas_disk(c, x + 27, y + 27, 15, BG);
    for (int column = 0; column < 12; ++column)
        jelli_canvas_rect(c, x + 22 + column, y + 16 + column, 1, 23 - column * 2, 0xffffu);
}

void jelli_pet_menu_ring(Canvas *c, const JelliGame *game, const JelliPetUi *ui)
{
    const JelliPetRenderKey *v = &ui->last_view;
    if (!v->ring_visible)
        return;
    for (unsigned slot = 1; slot <= 6u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_ring_button(ui, slot, game->pets[game->active].asleep, &b))
            continue;
        uint16_t edge =
            v->ring_page == JELLI_UI_MOMENTS &&
                    slot == jelli_pet_suggested_moment(&game->pets[game->active], ui) + 1u
                ? GOLD
                : 0x738eu;
        int factor = 510 - v->ring_visible;
        int x = 233 + ((int)b.bounds.x + 48 - 233) * factor / 255;
        int y = 233 + ((int)b.bounds.y + 48 - 233) * factor / 255;
        c->dim = v->ring_page == ui->page && v->ring_clock_edit == ui->clock_edit &&
                 (v->unavailable & (1u << slot));
        jelli_canvas_disk(c, x, y, 48, edge);
        jelli_canvas_disk(c, x, y, 46, 0x4a49u);
        if (b.icon)
            jelli_canvas_centered_sprite(c, b.icon, x, y - 17,
                                         jelli_canvas_fit_scale(b.icon, RING_ICON_PX));
        c->dim = false; /* Disabled controls retain readable action labels. */
        button_label(c, b.label, x, y + (b.icon ? 7 : -12));
        if (jelli_pet_ui_starts(v->ring_page, slot))
            start_badge(c, x, y - 54);
        c->dim = false;
    }
}

const char *jelli_pet_menu_title(const JelliPetUi *ui)
{
    static const char *const titles[] = {"MENU",     "CARE",       "MORE",   "PRESENTS",
                                         "SETTINGS", "ACTIVITIES", "HEALTH", "BRUSH",
                                         "MEDICINE", "SHOT",       "BATH",   "STRETCH"};
    if (ui->page == JELLI_UI_SETTINGS && ui->clock_edit)
        return "CLOCK";
    return (unsigned)ui->page < sizeof(titles) / sizeof(titles[0]) ? titles[ui->page] : "MENU";
}

static const char *exercise_hint(const JelliPet *pet)
{
    if (pet->needs[JELLI_SATIETY] < jelli_exercise.fullness_cost)
        return "CARE > FEED FIRST";
    if (pet->hydration < jelli_exercise.hydration_cost)
        return "CARE > WATER FIRST";
    if (pet->needs[JELLI_ENERGY] < jelli_exercise.energy_cost)
        return "REST FOR ENERGY";
    return "TRY AGAIN LATER";
}

static const char *not_ready_hint(const JelliPetUi *ui, const JelliGame *game)
{
    if (ui->page == JELLI_UI_HEALTH && (ui->attempted_slot == 2u || ui->attempted_slot == 3u))
        return "DOSE GIVEN - WAIT";
    if (ui->page == JELLI_UI_MOMENTS && ui->attempted_slot == 5u)
        return exercise_hint(&game->pets[game->active]);
    if (ui->page == JELLI_UI_MORE && ui->attempted_slot == 2u)
        return "NO REWARD YET";
    if (ui->page == JELLI_UI_SETTINGS && ui->clock_edit)
        return "TIME ZONE LIMIT";
    return "TRY AGAIN LATER";
}

const char *jelli_pet_menu_hint(const JelliPetUi *ui, const JelliGame *game)
{
    if (ui->result == JELLI_ASLEEP)
        return "SETTINGS > WAKE";
    if (ui->result == JELLI_BUSY)
        return "WAIT FOR ACTIVITY";
    if (ui->result == JELLI_NOT_READY)
        return not_ready_hint(ui, game);
    if (ui->result == JELLI_FULL && ui->page == JELLI_UI_CARE && ui->attempted_slot == 6u)
        return "HYDRATION FULL";
    if (ui->result == JELLI_NO_ITEM && ui->page == JELLI_UI_MOMENTS)
        return "FEED > GET FOOD";
    if (ui->result == JELLI_FULL && ui->page == JELLI_UI_SETTINGS)
        return game->volume ? "VOLUME MAX" : "MUTED";
    if (ui->result == JELLI_NO_ITEM && ui->page == JELLI_UI_MORE)
        return "NO GIFTS LEFT";
    if (ui->result != JELLI_OK)
        return "";
    if (ui->page == JELLI_UI_HOME)
        return "CHOOSE A MENU";
    if (ui->page == JELLI_UI_HEALTH)
        return "TAP TO START";
    return "";
}

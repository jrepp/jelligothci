#include "pet_menu.h"
#include "jelli/activities.h"
#include "jelli/nutrition.h"
#include <string.h>

#define RING_ICON_PX 32u /* Ring item icons draw at about this size. */

static void label_line(Canvas *c, const char *text, int x, int y)
{
    int width = (int)strlen(text) * 16;
    int left = x - width / 2;
    int inset = 18;
    while (inset < 233 &&
           (!jelli_canvas_in_round(inset, y - 1) || !jelli_canvas_in_round(inset, y + 24)))
        ++inset;
    if (left < inset)
        left = inset;
    if (left + width > 466 - inset)
        left = 466 - inset - width;
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
        uint16_t edge = jelli_pet_discovery_badge(ui, v->ring_page, slot) &&
                                jelli_pet_activity_choice(ui, slot) ==
                                    jelli_pet_suggested_moment(&game->pets[game->active], ui)
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
        if (v->ring_page == JELLI_UI_HOME && slot == 5u && v->discovery_icon) {
            int radius = 7 + (v->discovery_pulse == 2u ? 3 : 0);
            jelli_canvas_disk(c, x + 33, y - 33, radius, GOLD);
        }
        if (jelli_pet_discovery_badge(ui, v->ring_page, slot)) {
            unsigned id = jelli_pet_activity_choice(ui, slot);
            if (id < jelli_moment_count && (ui->ready_offers & (UINT32_C(1) << id))) {
                jelli_canvas_disk(c, x - 32, y - 32, 9, GOLD);
                jelli_canvas_rect(c, x - 33, y - 38, 2, 7, INK);
                jelli_canvas_rect(c, x - 32, y - 32, 5, 2, INK);
            }
        }
        c->dim = false;
    }
}

const char *jelli_pet_menu_title(const JelliPetUi *ui)
{
    static const char *const titles[] = {"MENU",     "CARE",       "PET MENU", "PRESENTS",
                                         "SETTINGS", "ACTIVITIES", "CARE",     "BRUSH",
                                         "MEDICINE", "SHOT",       "BATH",     "YOGA"};
    if (ui->location_menu && ui->page == JELLI_UI_MOMENTS)
        return "LOCATIONS";
    if (ui->quick_menu && ui->page == JELLI_UI_MOMENTS)
        return "QUICK CARE";
    if (ui->page == JELLI_UI_SETTINGS && ui->clock_edit)
        return "CLOCK";
    return (unsigned)ui->page < sizeof(titles) / sizeof(titles[0]) ? titles[ui->page] : "MENU";
}

static const char *exercise_hint(const JelliPet *pet)
{
    if (pet->needs[JELLI_SATIETY] < jelli_exercise.fullness_cost)
        return "FOOD FIRST";
    if (pet->hydration <
        jelli_activity_cost(pet, JELLI_ACTIVITY_HYDRATION, jelli_exercise.hydration_cost))
        return "WATER FIRST";
    if (pet->needs[JELLI_ENERGY] <
        jelli_activity_cost(pet, JELLI_ENERGY, jelli_exercise.energy_cost))
        return "REST FOR ENERGY";
    return "TRY AGAIN LATER";
}

static const char *not_ready_hint(const JelliPetUi *ui, const JelliGame *game)
{
    if (ui->quick_menu)
        return "NOT READY NOW";
    if (ui->page == JELLI_UI_HEALTH && (ui->attempted_slot == 2u || ui->attempted_slot == 3u))
        return "ALL DONE - WAIT";
    if (ui->page == JELLI_UI_MOMENTS ||
        (ui->page == JELLI_UI_HOME && (ui->attempted_slot == 1u || ui->attempted_slot == 6u))) {
        unsigned id = ui->page == JELLI_UI_HOME ? jelli_pet_home_choice(ui, ui->attempted_slot)
                                                : jelli_pet_activity_choice(ui, ui->attempted_slot);
        uint32_t offers = ui->page == JELLI_UI_HOME ? ui->home_offers : ui->activity_offers;
        if (id < jelli_moment_count && (offers & (UINT32_C(1) << id)) &&
            !(game->pets[game->active].invitations.pending & (UINT32_C(1) << id)))
            return "OFFER ENDED";
        if (id == jelli_moment_count)
            return exercise_hint(&game->pets[game->active]);
        if (id == jelli_moment_count + 1u)
            return "NOT READY NOW";
        return jelli_moment_hint(game, &game->pets[game->active], id);
    }
    if (ui->page == JELLI_UI_MORE && ui->attempted_slot == 2u)
        return "NO REWARD YET";
    if (ui->page == JELLI_UI_SETTINGS && ui->clock_edit)
        return "TIME ZONE LIMIT";
    return "TRY AGAIN LATER";
}

const char *jelli_pet_menu_hint(const JelliPetUi *ui, const JelliGame *game)
{
    if (ui->result == JELLI_ASLEEP)
        return "CARE > WAKE";
    if (ui->result == JELLI_BUSY)
        return "WAIT FOR ACTIVITY";
    if (ui->result == JELLI_NOT_READY)
        return not_ready_hint(ui, game);
    if (ui->result == JELLI_FULL && ((ui->page == JELLI_UI_CARE && ui->attempted_slot == 6u) ||
                                     (ui->page == JELLI_UI_HOME && ui->attempted_slot == 3u)))
        return "NOT THIRSTY";
    if (ui->result == JELLI_FULL && ui->page == JELLI_UI_HOME && ui->attempted_slot == 2u)
        return "TUMMY FULL";
    if (ui->result == JELLI_FULL && ui->page == JELLI_UI_SETTINGS)
        return game->volume ? "VOLUME MAX" : "MUTED";
    if (ui->result == JELLI_NO_ITEM && ui->page == JELLI_UI_MORE)
        return "NO GIFTS LEFT";
    if (ui->result != JELLI_OK)
        return "";
    if (ui->page == JELLI_UI_HOME)
        return "CHOOSE AN ACTION";
    if (ui->page == JELLI_UI_HEALTH)
        return "TAP TO START";
    return "";
}

void jelli_pet_discovery_draw(Canvas *c, const JelliPetUi *ui, const JelliPetRenderKey *v)
{
    if (ui->menu_open)
        return;
    if (v->hold_progress) {
        jelli_canvas_rect(c, 173, 245, 120, 8, INK);
        jelli_canvas_rect(c, 173, 245, (int)v->hold_progress * 120 / 100, 8, GOLD);
    }
    if (!v->discovery_icon || v->discovery_icon > jelli_moment_count)
        return;
    int x = 330, y = 135 - (v->discovery_pulse == 2u ? 3 : 0);
    jelli_canvas_disk(c, 295, 183, 5, PALE);
    jelli_canvas_disk(c, 307, 169, 8, PALE);
    jelli_canvas_disk(c, x, y, 35, INK);
    jelli_canvas_disk(c, x, y, 32, PALE);
    uint32_t icon = jelli_moments[v->discovery_icon - 1u].icon;
    jelli_canvas_centered_sprite(c, icon, x, y, jelli_canvas_fit_scale(icon, 42u));
}

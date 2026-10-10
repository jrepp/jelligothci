#include "pet_draw.h"
#include "pet_canvas.h"
#include "pet_gallery.h"
#include "pet_collection.h"
#include "jelli/assets.h"
#include <stdio.h>
#include <string.h>

#ifndef JELLI_VERSION_LABEL
#define JELLI_VERSION_LABEL "UNVERSIONED" /* Standalone static-analysis build. */
#endif

static void tile(Canvas *c, const JelliPetRenderKey *v, unsigned index, int x)
{
    static const char *const labels[] = {"MOOD",   "FULLNESS", "ENERGY", "HYGIENE",  "PLAY",
                                         "SOCIAL", "BOND",     "SLEEP",  "HYDRATION"};
    index %= JELLI_PET_STAT_COUNT;
    unsigned score = index == 8u ? v->stat_value / 10u : jelli_pet_stat_score(v->stat_value);
    jelli_canvas_rect(c, x, 284, 286, 96, INK);
    jelli_canvas_rect(c, x + 4, 288, 278, 88, BG);
    jelli_canvas_rect(c, x + 12, 296, 76, 72, INK);
    int filled = (int)(score * 72u / 100u);
    jelli_canvas_rect(c, x + 12, 368 - filled, 76, filled, score < 25u ? PINK : TEAL);
    uint32_t icon = index == 8u   ? 6015u
                    : index == 7u ? 2005u
                    : index == 6u ? 9002u
                    : !index      ? 7005u
                                  : 7000u + index;
    jelli_canvas_sprite(c, icon, x + 18, 300, 2u);
    char number[4];
    int size = snprintf(number, sizeof(number), "%u", score);
    if (size > 0 && (size_t)size < sizeof(number))
        jelli_canvas_text(c, number, x + 112, 287, 4u, PALE);
    jelli_canvas_text(c, labels[index], x + 112, 344, 2u, MINT);
}

static void draw_tile(Canvas c, const JelliPetRenderKey *view)
{
    if (c.left < 90)
        c.left = 90;
    if (c.top < 282)
        c.top = 282;
    if (c.right > 376)
        c.right = 376;
    if (c.bottom > 382)
        c.bottom = 382;
    jelli_canvas_rect(&c, 90, 282, 286, 100, BG);
    tile(&c, view, view->stat_index, 90);
}

void jelli_pet_draw_tile(JelliSurface *surface, const JelliPetRenderKey *view)
{
    Canvas c = {surface, 90, 282, 376, 382, 0, false, view->assets};
    draw_tile(c, view);
}

static const char *result_status(JelliResult result)
{
    switch (result) {
    case JELLI_OK:
        return "";
    case JELLI_BUSY:
        return "BUSY";
    case JELLI_NO_ITEM:
        return "NO ITEMS";
    case JELLI_ASLEEP:
        return "WAKE FIRST";
    case JELLI_FULL:
        return "ALL SET";
    case JELLI_NOT_READY:
        return "NOT READY";
    case JELLI_INVALID_TARGET:
        return "TRY AGAIN";
    }
    return "TRY AGAIN";
}

static const char *status(const JelliPetRenderKey *v)
{
    if (v->resuming)
        return "RESUMING";
    if (v->paused)
        return "PAUSED";
    if (v->result != JELLI_OK)
        return result_status(v->result);
    if (v->asleep)
        return "ASLEEP";
    if (v->reaction == 1u)
        return "THAT IS NICE";
    if (v->reaction == 2u)
        return "A LITTLE SPACE";
    if (v->reaction == 3u)
        return "TOO MUCH";
    if (v->health == JELLI_RECOVERING)
        return "RECOVERING";
    if (v->health == JELLI_UNWELL)
        return "NEEDS CARE";
    if (v->activity == JELLI_EATING)
        return "YUM!";
    if (v->activity == JELLI_PLAYING)
        return "ENJOYING";
    if (v->activity == JELLI_GIVING)
        return "THANK YOU";
    return "";
}

static void start_badge(Canvas *c, int x, int y)
{
    jelli_canvas_disk(c, x + 27, y + 27, 15, BG);
    for (int column = 0; column < 12; ++column)
        jelli_canvas_rect(c, x + 22 + column, y + 16 + column, 1, 23 - column * 2, 0xffffu);
}

static void ring(Canvas *c, const JelliGame *game, const JelliPetUi *ui)
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
            jelli_canvas_centered_sprite(c, b.icon, x, y, b.scale);
        else
            jelli_canvas_text(c, b.label, x - (int)strlen(b.label) * 8, y - 12, 2u, PALE);
        if (jelli_pet_ui_starts(v->ring_page, slot))
            start_badge(c, x, y);
        if (v->ring_page == JELLI_UI_SETTINGS && !v->ring_clock_edit && slot == 3u) {
            jelli_canvas_rect(c, x - 28, y + 20, 56, 24, v->asleep ? TEAL : BG);
            jelli_canvas_text(c, v->asleep ? "ON" : "OFF", x - (v->asleep ? 16 : 24), y + 20, 2u,
                              0xffffu);
        }
        c->dim = false;
    }
}

static void menu_button(Canvas *c, const JelliPetUi *ui)
{
    if (ui->menu_open) {
        jelli_canvas_disk(c, 233, 462, 48, TEAL);
        jelli_canvas_centered_sprite(c, ui->page == JELLI_UI_HOME ? 6013u : 2011u, 233, 440,
                                     ui->page == JELLI_UI_HOME ? 1u : 2u);
        return;
    }
    jelli_canvas_rect(c, 193, 388, 80, 64, TEAL);
    jelli_canvas_disk(c, 193, 420, 32, TEAL);
    jelli_canvas_disk(c, 273, 420, 32, TEAL);
    JelliPetUiButton button;
    if (!jelli_pet_ui_control(ui, 0u, false, &button))
        return;
    int width = 40 + (int)(strlen(button.label) * 16u);
    int left = 233 - width / 2;
    jelli_canvas_centered_sprite(c, button.icon, left + 16, 419, button.scale);
    jelli_canvas_text(c, button.label, left + 40, 407, 2u, PALE);
}

static void page_info(Canvas *c, const JelliGame *game, const JelliPetUi *ui)
{
    const JelliPet *pet = &game->pets[game->active];
    char value[18];
    const char *label = status(&ui->last_view);
    if (ui->page == JELLI_UI_SETTINGS) {
        int zone = ui->timezone_minutes;
        int size = ui->clock_edit
                       ? snprintf(value, sizeof(value), "TZ%c%02d:%02d", zone < 0 ? '-' : '+',
                                  (zone < 0 ? -zone : zone) / 60, (zone < 0 ? -zone : zone) % 60)
                       : snprintf(value, sizeof(value), "BED %02u:00", (unsigned)pet->bedtime);
        if (size > 0 && (size_t)size < sizeof(value))
            jelli_canvas_caption(c, value, 304, 0xffffu);
        label = JELLI_VERSION_LABEL;
    } else if (ui->page == JELLI_UI_MORE) {
        int size = snprintf(value, sizeof(value), "GIFTS %u", (unsigned)game->gifts);
        if (size > 0 && (size_t)size < sizeof(value))
            jelli_canvas_centered(c, value, 322, 1u, MINT);
        label = pet->reward_pending ? "CLAIM!" : "";
    } else if (ui->page == JELLI_UI_MOMENTS) {
        static const char *const names[] = {"BREAKFAST", "TEA", "GOING OUT", "MOVIE"};
        jelli_canvas_centered(c, ui->clock_known ? "FOR NOW" : "PET TIME", 322, 1u, GOLD);
        label = names[jelli_pet_suggested_moment(pet, ui)];
    }
    jelli_canvas_caption(c, label, 346, PALE);
}

static void settings_clock(Canvas *c, const JelliPetRenderKey *view)
{
    jelli_canvas_disk(c, 233, 215, 74, 0x738eu);
    jelli_canvas_disk(c, 233, 215, 70, BG);
    unsigned minute = view->clock_minute;
    char value[6];
    (void)snprintf(value, sizeof(value), "%02u:%02u", minute / 60u % 24u, minute % 60u);
    jelli_canvas_centered(c, value, 190, 3u, 0xffffu);
    jelli_canvas_centered(c, view->clock_known ? "LOCAL" : "PET", 225, 2u, 0xffffu);
    if (!view->clock_edit)
        jelli_canvas_centered_sprite(c, 6006u, 233, 267, 1u);
}

static void activity(Canvas *c, const JelliPetUi *ui)
{
    JelliPetUiButton b;
    if (!jelli_pet_ui_control(ui, 1u, false, &b))
        return;
    int x = (int)b.bounds.x + 48, y = (int)b.bounds.y + 48;
    jelli_canvas_disk(c, x, y, 48, 0x738eu);
    jelli_canvas_disk(c, x, y, 46, 0x4a49u);
    c->dim = (ui->last_view.unavailable & 2u) != 0u;
    jelli_canvas_centered_sprite(c, b.icon, x, y, b.scale);
    c->dim = false;
    for (unsigned i = 0; i < ui->clicker_goal; ++i)
        jelli_canvas_disk(
            c, 233 - (int)(ui->clicker_goal ? ui->clicker_goal - 1u : 0u) * 10 + (int)i * 20, 366,
            5, i < ui->clicker_hits ? GOLD : INK);
    if (ui->result == JELLI_OK && !ui->clicker_done)
        jelli_canvas_heading(c, b.label, 82, 1u);
    if (ui->result != JELLI_OK)
        jelli_canvas_heading(c, status(&ui->last_view), 82, 1u);
    else if (ui->clicker_done)
        jelli_canvas_heading(c, "WELL DONE!", 82, 1u);
}

void jelli_pet_draw_region(JelliSurface *surface, const JelliGame *game, const JelliPetUi *ui,
                           const JelliPetRenderKey *view, JelliRect region)
{
    Canvas c = {surface,
                (int)region.x,
                (int)region.y,
                (int)(region.x + region.width),
                (int)(region.y + region.height),
                0,
                false,
                view->assets};
    jelli_pet_draw_background(surface, view, region);
    c.icon_night = view->night;
    if (ui->menu_open && ui->page >= JELLI_UI_PETS) {
        if (ui->page == JELLI_UI_PRESENT_ACTION)
            jelli_pet_gallery_draw_action(&c, ui, game);
        else
            jelli_pet_collection_draw(&c, ui, game);
        menu_button(&c, ui);
        return;
    }
    if (ui->menu_open && ui->page == JELLI_UI_COLLECTION) {
        jelli_pet_gallery_draw(&c, view);
        menu_button(&c, ui);
        return;
    }
    jelli_canvas_heading(&c, view->form ? "LILAC" : "MINT", 16, 3u);
    jelli_canvas_heading(&c, view->location ? "@ GARDEN" : "@ HOME", 57, 2u);
    if (ui->menu_open && ui->page == JELLI_UI_SETTINGS)
        settings_clock(&c, view);
    else if (ui->actor_frame)
        jelli_canvas_sprite(&c, ui->actor_frame->id, ui->actor_x, ui->actor_y, 6u);
    c.icon_night = view->night;
    if ((ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH)) {
        activity(&c, ui);
    } else if (ui->menu_open) {
        if (!view->ring_moving)
            page_info(&c, game, ui);
    } else {
        jelli_canvas_caption(&c, status(view), 254, MINT);
        draw_tile(c, view);
        jelli_pet_gallery_draw_latched(&c, view);
    }
    ring(&c, game, ui);
    menu_button(&c, ui);
}

void jelli_pet_draw(JelliSurface *surface, const JelliGame *game, const JelliPetUi *ui,
                    const JelliPetRenderKey *view)
{
    jelli_pet_draw_region(surface, game, ui, view, (JelliRect){0, 0, JELLI_WIDTH, JELLI_HEIGHT});
}

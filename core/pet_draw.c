#include "pet_draw.h"
#include "jelli/assets.h"
#include <stdio.h>
#include <string.h>

#ifndef JELLI_VERSION_LABEL
#define JELLI_VERSION_LABEL "UNVERSIONED" /* Standalone static-analysis build. */
#endif

#define BG 0x18e3u
#define INK 0x4989u
#define PALE 0xff99u
#define MINT 0x8736u
#define TEAL 0x1bcfu
#define GOLD 0xf62cu
#define PINK 0xfc73u

typedef struct {
    JelliSurface *s;
    int left, top, right, bottom;
    uint8_t icon_night;
    bool dim;
} Canvas;

static bool in_round(int x, int y)
{
    int dx = 2 * x - 465, dy = 2 * y - 465;
    return dx * dx + dy * dy <= 466 * 466;
}
static void pixel(Canvas *c, int x, int y, uint16_t color)
{
    if (c->dim)
        color = (uint16_t)((color >> 1) & 0x7befu);
    if (x >= c->left && y >= c->top && x < c->right && y < c->bottom && in_round(x, y))
        c->s->pixels[(unsigned)y * c->s->stride + (unsigned)x] = color;
}
static void rect(Canvas *c, int x, int y, int w, int h, uint16_t color)
{
    int right = x + w < c->right ? x + w : c->right;
    int bottom = y + h < c->bottom ? y + h : c->bottom;
    if (x < c->left)
        x = c->left;
    if (y < c->top)
        y = c->top;
    for (int row = y; row < bottom; ++row)
        for (int column = x; column < right; ++column)
            pixel(c, column, row, color);
}
static void disk(Canvas *c, int x, int y, int radius, uint16_t color)
{
    int left = x - radius < c->left ? c->left - x : -radius;
    int right = x + radius >= c->right ? c->right - x - 1 : radius;
    int top = y - radius < c->top ? c->top - y : -radius;
    int bottom = y + radius >= c->bottom ? c->bottom - y - 1 : radius;
    for (int row = top; row <= bottom; ++row)
        for (int column = left; column <= right; ++column)
            if (row * row + column * column <= radius * radius)
                pixel(c, x + column, y + row, color);
}
static void text(Canvas *c, const char *value, int x, int y, unsigned scale, uint16_t color)
{
    while (*value) {
        const uint8_t *rows = jelli_asset_glyph((uint8_t)*value++);
        for (unsigned row = 0; row < 12u; ++row)
            for (unsigned column = 0; column < 8u; ++column)
                if (rows[row] & (uint8_t)(1u << (7u - column)))
                    rect(c, x + (int)(column * scale), y + (int)(row * scale), (int)scale,
                         (int)scale, color);
        x += (int)(8u * scale);
    }
}
static void centered(Canvas *c, const char *value, int y, unsigned scale, uint16_t color)
{
    int width = (int)(strlen(value) * 8u * scale);
    text(c, value, 233 - width / 2, y, scale, color);
}
static void sprite(Canvas *c, uint32_t id, int x, int y, unsigned scale)
{
    const JelliAsset *a = jelli_asset_find(id);
    if (!a || !a->pixels || !a->mask)
        return;
    if (x >= c->right || y >= c->bottom || x + (int)(a->width * scale) <= c->left ||
        y + (int)(a->height * scale) <= c->top)
        return;
    for (unsigned row = 0; row < a->height; ++row)
        for (unsigned column = 0; column < a->width; ++column)
            if (a->mask[row * a->mask_stride + column / 8u] & (uint8_t)(1u << (7u - column % 8u)))
                rect(c, x + (int)(column * scale), y + (int)(row * scale), (int)scale, (int)scale,
                     jelli_pet_night_color(a->pixels[row * a->width + column], c->icon_night));
}

static void centered_sprite(Canvas *c, uint32_t id, int x, int y, unsigned scale)
{
    const JelliAsset *a = jelli_asset_find(id);
    if (!a)
        return;
    sprite(c, id, x - (int)((a->centroid_x_q8 * scale + 128u) / 256u),
           y - (int)((a->centroid_y_q8 * scale + 128u) / 256u), scale);
}

static void heading(Canvas *c, const char *value, int y, unsigned scale)
{
    int x = 233 - (int)(strlen(value) * 8u * scale) / 2;
    text(c, value, x + 2, y + 2, scale, 0u);
    text(c, value, x, y, scale, 0xffffu);
}

/* A soft, bounded scrim follows the caption, with no scratch buffer. */
static void caption(Canvas *c, const char *value, int y, uint16_t color)
{
    if (!*value)
        return;
    int half = (int)strlen(value) * 8 + 16;
    for (int dy = -8; dy < 32; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
            int x = 233 + dx, row = y + dy;
            if (x < c->left || row < c->top || x >= c->right || row >= c->bottom ||
                !in_round(x, row))
                continue;
            int edge = half - (dx < 0 ? -dx : dx);
            int vertical = dy < 12 ? dy + 8 : 31 - dy;
            unsigned fade = (unsigned)(edge < vertical ? edge : vertical);
            unsigned shade = 16u - (fade > 8u ? 8u : fade);
            uint16_t back = c->s->pixels[(unsigned)row * c->s->stride + (unsigned)x];
            unsigned r = ((back >> 11) & 31u) * shade / 16u;
            unsigned g = ((back >> 5) & 63u) * shade / 16u;
            unsigned b = (back & 31u) * shade / 16u;
            pixel(c, x, row, (uint16_t)((r << 11) | (g << 5) | b));
        }
    }
    centered(c, value, y, 2u, color);
}

static void tile(Canvas *c, const JelliPetRenderKey *v, unsigned index, int x)
{
    static const char *const labels[] = {"MOOD", "FULLNESS", "ENERGY", "HYGIENE", "PLAY", "SOCIAL"};
    index %= 6u;
    unsigned score = index ? jelli_pet_stat_score(v->needs[index - 1u]) : v->mood;
    rect(c, x, 284, 286, 96, INK);
    rect(c, x + 4, 288, 278, 88, BG);
    rect(c, x + 12, 296, 76, 72, INK);
    int filled = (int)(score * 72u / 100u);
    rect(c, x + 12, 368 - filled, 76, filled, score < 25u ? PINK : TEAL);
    sprite(c, index ? 7000u + index : 7005u, x + 18, 300, 2u);
    char number[4];
    int size = snprintf(number, sizeof(number), "%u", score);
    if (size > 0 && (size_t)size < sizeof(number))
        text(c, number, x + 112, 287, 4u, PALE);
    text(c, labels[index], x + 112, 344, 2u, MINT);
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
    rect(&c, 90, 282, 286, 100, BG);
    tile(&c, view, view->stat_index, 90);
}

void jelli_pet_draw_tile(JelliSurface *surface, const JelliPetRenderKey *view)
{
    Canvas c = {surface, 90, 282, 376, 382, 0, false};
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
    disk(c, x + 27, y + 27, 15, BG);
    for (int column = 0; column < 12; ++column)
        rect(c, x + 22 + column, y + 16 + column, 1, 23 - column * 2, 0xffffu);
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
        disk(c, x, y, 48, edge);
        disk(c, x, y, 46, 0x4a49u);
        if (b.icon)
            centered_sprite(c, b.icon, x, y, b.scale);
        else
            text(c, b.label, x - (int)strlen(b.label) * 8, y - 12, 2u, PALE);
        if (jelli_pet_ui_starts(v->ring_page, slot))
            start_badge(c, x, y);
        if (v->ring_page == JELLI_UI_SETTINGS && !v->ring_clock_edit && slot == 3u) {
            rect(c, x - 28, y + 20, 56, 24, v->asleep ? TEAL : BG);
            text(c, v->asleep ? "ON" : "OFF", x - (v->asleep ? 16 : 24), y + 20, 2u, 0xffffu);
        }
        c->dim = false;
    }
}

static void menu_button(Canvas *c, const JelliPetUi *ui)
{
    if (ui->menu_open) {
        disk(c, 233, 462, 48, TEAL);
        centered_sprite(c, ui->page == JELLI_UI_HOME ? 6013u : 2011u, 233, 440,
                        ui->page == JELLI_UI_HOME ? 1u : 2u);
        return;
    }
    rect(c, 193, 388, 80, 64, TEAL);
    disk(c, 193, 420, 32, TEAL);
    disk(c, 273, 420, 32, TEAL);
    JelliPetUiButton button;
    if (!jelli_pet_ui_control(ui, 0u, false, &button))
        return;
    int width = 40 + (int)(strlen(button.label) * 16u);
    int left = 233 - width / 2;
    centered_sprite(c, button.icon, left + 16, 419, button.scale);
    text(c, button.label, left + 40, 407, 2u, PALE);
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
            caption(c, value, 304, 0xffffu);
        label = JELLI_VERSION_LABEL;
    } else if (ui->page == JELLI_UI_MORE) {
        int size = snprintf(value, sizeof(value), "GIFTS %u", (unsigned)game->gifts);
        if (size > 0 && (size_t)size < sizeof(value))
            centered(c, value, 322, 1u, MINT);
        label = pet->reward_pending ? "CLAIM!" : "";
    } else if (ui->page == JELLI_UI_MOMENTS) {
        static const char *const names[] = {"BREAKFAST", "TEA", "GOING OUT", "MOVIE"};
        centered(c, ui->clock_known ? "FOR NOW" : "PET TIME", 322, 1u, GOLD);
        label = names[jelli_pet_suggested_moment(pet, ui)];
    }
    caption(c, label, 346, PALE);
}

static void settings_clock(Canvas *c, const JelliPetRenderKey *view)
{
    disk(c, 233, 215, 74, 0x738eu);
    disk(c, 233, 215, 70, BG);
    unsigned minute = view->clock_minute;
    char value[6];
    (void)snprintf(value, sizeof(value), "%02u:%02u", minute / 60u % 24u, minute % 60u);
    centered(c, value, 190, 3u, 0xffffu);
    centered(c, view->clock_known ? "LOCAL" : "PET", 225, 2u, 0xffffu);
    if (!view->clock_edit)
        centered_sprite(c, 6006u, 233, 267, 1u);
}

static void activity(Canvas *c, const JelliPetUi *ui)
{
    JelliPetUiButton b;
    if (!jelli_pet_ui_control(ui, 1u, false, &b))
        return;
    int x = (int)b.bounds.x + 48, y = (int)b.bounds.y + 48;
    disk(c, x, y, 48, 0x738eu);
    disk(c, x, y, 46, 0x4a49u);
    c->dim = (ui->last_view.unavailable & 2u) != 0u;
    centered_sprite(c, b.icon, x, y, b.scale);
    c->dim = false;
    for (unsigned i = 0; i < ui->clicker_goal; ++i)
        disk(c, 233 - (int)(ui->clicker_goal ? ui->clicker_goal - 1u : 0u) * 10 + (int)i * 20, 366,
             5, i < ui->clicker_hits ? GOLD : INK);
    if (ui->result == JELLI_OK && !ui->clicker_done)
        heading(c, b.label, 82, 1u);
    if (ui->result != JELLI_OK)
        heading(c, status(&ui->last_view), 82, 1u);
    else if (ui->clicker_done)
        heading(c, "WELL DONE!", 82, 1u);
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
                false};
    jelli_pet_draw_background(surface, view, region);
    heading(&c, view->form ? "LILAC" : "MINT", 16, 3u);
    heading(&c, view->location ? "@ GARDEN" : "@ HOME", 57, 2u);
    if (ui->menu_open && ui->page == JELLI_UI_SETTINGS)
        settings_clock(&c, view);
    else if (ui->actor_frame)
        sprite(&c, ui->actor_frame->id, ui->actor_x, ui->actor_y, 6u);
    c.icon_night = view->night;
    if (ui->page >= JELLI_UI_BRUSH) {
        activity(&c, ui);
    } else if (ui->menu_open) {
        if (!view->ring_moving)
            page_info(&c, game, ui);
    } else {
        caption(&c, status(view), 254, MINT);
        draw_tile(c, view);
    }
    ring(&c, game, ui);
    menu_button(&c, ui);
}

void jelli_pet_draw(JelliSurface *surface, const JelliGame *game, const JelliPetUi *ui,
                    const JelliPetRenderKey *view)
{
    jelli_pet_draw_region(surface, game, ui, view, (JelliRect){0, 0, JELLI_WIDTH, JELLI_HEIGHT});
}

#include "jelli/pet_ui.h"
#include "jelli/assets.h"
#include <string.h>
#include <stdio.h>

#define BLACK 0x18e3u
#define INK 0x39ebu
#define PALE 0xef5bu
#define MINT 0x86f5u
#define TEAL 0x2eecu
#define PINK 0xe4f3u
#define GOLD 0xf64bu
#define WHITE 0xffffu

static bool in_round(unsigned x, unsigned y)
{
    int dx = 2 * (int)x - 465;
    int dy = 2 * (int)y - 465;
    return dx * dx + dy * dy <= 466 * 466;
}

static void pixel(JelliSurface *s, int x, int y, uint16_t color)
{
    if (x >= 0 && y >= 0 && x < (int)s->width && y < (int)s->height &&
        in_round((unsigned)x, (unsigned)y))
        s->pixels[(unsigned)y * s->stride + (unsigned)x] = color;
}

static void rect(JelliSurface *s, int x, int y, int width, int height, uint16_t color)
{
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column)
            pixel(s, x + column, y + row, color);
}

static void glyph(JelliSurface *s, uint8_t code, int x, int y, unsigned scale, uint16_t color)
{
    const uint8_t *rows = jelli_asset_glyph(code);
    for (unsigned row = 0u; row < 12u; ++row) {
        for (unsigned column = 0u; column < 8u; ++column) {
            if ((rows[row] & (uint8_t)(1u << (7u - column))) != 0u)
                rect(s, x + (int)(column * scale), y + (int)(row * scale), (int)scale, (int)scale,
                     color);
        }
    }
}

static int draw_text(JelliSurface *s, const char *text, int x, int y, unsigned scale,
                     uint16_t color)
{
    int start = x;
    while (*text != '\0') {
        uint8_t code = (uint8_t)*text++;
        if (code >= (uint8_t)'a' && code <= (uint8_t)'z')
            code = (uint8_t)(code - (uint8_t)'a' + (uint8_t)'A');
        glyph(s, code, x, y, scale, color);
        x += (int)(8u * scale);
    }
    return x - start;
}

static void centered(JelliSurface *s, const char *text, int center, int y, uint16_t color)
{
    int width = (int)(strlen(text) * 8u);
    draw_text(s, text, center - width / 2, y, 1u, color);
}

static void sprite(JelliSurface *s, uint32_t id, int x, int y, unsigned scale)
{
    const JelliAsset *asset = jelli_asset_find(id);
    if (asset == NULL || asset->pixels == NULL || asset->mask == NULL)
        return;
    for (unsigned row = 0u; row < asset->height; ++row) {
        for (unsigned column = 0u; column < asset->width; ++column) {
            unsigned bit = 7u - column % 8u;
            if ((asset->mask[row * asset->mask_stride + column / 8u] & (uint8_t)(1u << bit)) == 0u)
                continue;
            uint16_t color = asset->pixels[row * asset->width + column];
            rect(s, x + (int)(column * scale), y + (int)(row * scale), (int)scale, (int)scale,
                 color);
        }
    }
}

static uint32_t asset_id(const JelliPet *pet, uint32_t phase)
{
    uint32_t base = pet->form == 0u ? 1000u : 1006u;
    if (pet->asleep)
        return base + 5u;
    if (pet->health == JELLI_UNWELL)
        return base + 6u;
    if (pet->activity == JELLI_EATING)
        return base + 3u;
    if (pet->activity == JELLI_PLAYING)
        return base + 4u;
    if (pet->activity == JELLI_GIVING)
        return base + 4u;
    return base + 1u + phase % 2u;
}

static void draw_bar(JelliSurface *s, unsigned index, uint16_t value)
{
    static const char *names[] = {"SAT", "ENERGY", "CLEAN", "PLAY", "SOCIAL"};
    int y = 220 + (int)(index * 17u);
    draw_text(s, names[index], 78, y, 1u, PALE);
    rect(s, 146, y + 2, 220, 8, INK);
    uint16_t color = index == 0u ? GOLD : (index == 2u ? TEAL : MINT);
    rect(s, 146, y + 2, (int)((uint32_t)value * 220u / 1000u), 8, color);
}

static const char *page_title(JelliPetPage page)
{
    switch (page) {
    case JELLI_UI_HOME:
        return "HOME";
    case JELLI_UI_CARE:
        return "CARE";
    case JELLI_UI_MORE:
        return "MORE";
    case JELLI_UI_COLLECTION:
        return "COLLECTION";
    case JELLI_UI_SETTINGS:
        return "SETTINGS";
    default:
        return "HOME";
    }
}

static void draw_buttons(JelliSurface *s, JelliPetPage page, bool asleep)
{
    for (unsigned item = 0u; item < 6u; ++item) {
        unsigned column = item % 3u;
        unsigned row = item / 3u;
        int x = 58 + (int)(column * 116u);
        int y = 310 + (int)(row * 49u);
        JelliPetUiItem button = jelli_pet_ui_item(page, item, asleep);
        int width = (int)(strlen(button.label) * 8u);
        rect(s, x, y, 112, 44, INK);
        rect(s, x + 2, y + 2, 108, 40, item == 0u ? TEAL : BLACK);
        draw_text(s, button.label, x + (112 - width) / 2, y + 16, 1u, PALE);
    }
}

static void draw_pet_title(JelliSurface *s, const JelliPet *pet)
{
    const char *form = pet->form == 0u ? "BABY" : "GROWN";
    const char *location = pet->location == 0u ? "HOME" : "GARDEN";
    char title[32];
    int written = snprintf(title, sizeof(title), "PET %02u %s %s", (unsigned)(pet->id % 100u), form,
                           location);
    if (written < 0 || (size_t)written >= sizeof(title))
        return;
    centered(s, title, 233, 57, WHITE);
}

static void draw_clock(JelliSurface *s, const JelliPet *pet)
{
    uint64_t day = pet->ticks / JELLI_DAY_TICKS;
    uint64_t day_phase =
        (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) % JELLI_DAY_TICKS;
    unsigned minute = (unsigned)(day_phase / 600u);
    char label[20];
    int written = snprintf(label, sizeof(label), "DAY %02u %02u:%02u", (unsigned)(day % 100u),
                           minute / 60u, minute % 60u);
    if (written >= 0 && (size_t)written < sizeof(label))
        centered(s, label, 233, 190, PALE);
}

static void draw_companion_line(JelliSurface *s, const JelliGame *game, bool paused)
{
    uint8_t other = game->active == 0u ? 1u : 0u;
    const JelliPet *active = &game->pets[game->active];
    const char *form = game->pets[other].form == 0u ? "BABY" : "GROWN";
    const char *sleep = game->pets[other].asleep ? " ASLEEP" : "";
    const char *status = active->asleep ? "ASLEEP" : "WELL";
    if (active->activity == JELLI_GIVING)
        status = "GIVING";
    else if (active->health == JELLI_RECOVERING)
        status = "RECOVERING";
    else if (active->health == JELLI_UNWELL)
        status = "NEEDS CARE";
    char label[56];
    int written = snprintf(label, sizeof(label), "ACTIVE %02u/%02u STORED %02u %s%s %s",
                           (unsigned)game->active + 1u, (unsigned)game->count,
                           (unsigned)(game->pets[other].id % 100u), form, sleep, status);
    if (paused)
        centered(s, "PAUSED", 233, 178, GOLD);
    else if (written >= 0 && (size_t)written < sizeof(label))
        centered(s, label, 233, 178, MINT);
}

static void draw_needs(JelliSurface *s, const JelliPet *pet)
{
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        draw_bar(s, i, pet->needs[i]);
    char bond[16];
    int written = snprintf(bond, sizeof(bond), "BOND %04u", (unsigned)pet->bond);
    if (written >= 0 && (size_t)written < sizeof(bond))
        centered(s, bond, 233, 297, PALE);
}

static void draw_inventory(JelliSurface *s, const JelliGame *game, const JelliPetUi *ui)
{
    char label[56];
    int written;
    if (ui->page == JELLI_UI_SETTINGS) {
        const JelliPet *pet = &game->pets[game->active];
        written = snprintf(label, sizeof(label), "BEDTIME %02u:00", (unsigned)pet->bedtime);
    } else {
        const JelliPet *pet = &game->pets[game->active];
        const char *reward =
            pet->reward_pending ? "READY" : (pet->reward_claimed ? "CLAIMED" : "NONE");
        written = snprintf(label, sizeof(label), "FOOD %02u GIFT %02u REWARD %s",
                           (unsigned)game->food, (unsigned)game->gifts, reward);
    }
    if (written >= 0 && (size_t)written < sizeof(label))
        centered(s, label, 233, 207, PALE);
}

static void draw_footer(JelliSurface *s, const JelliGame *game, const JelliPetUi *ui, bool asleep)
{
    if (game->resuming)
        centered(s, "RESUMING", 233, 365, GOLD);
    else
        draw_buttons(s, ui->page, asleep);
    centered(s, jelli_game_result_name(ui->result), 233, 418, PINK);
    if (ui->time_unavailable)
        centered(s, "TIME UNKNOWN", 233, 438, GOLD);
    else if (ui->save_status == JELLI_SAVE_PENDING)
        centered(s, "SAVE PENDING", 233, 438, GOLD);
    else if (ui->save_status == JELLI_SAVE_SAVED)
        centered(s, "SAVED", 233, 438, MINT);
    else if (ui->save_status == JELLI_SAVE_FAILED)
        centered(s, "SAVE FAILED", 233, 438, PINK);
    else
        centered(s, "UNSAVED", 233, 438, PALE);
}

static void draw_scene(JelliSurface *s, const JelliGame *game, const JelliPetUi *ui, uint32_t phase,
                       bool paused)
{
    const JelliPet *pet = &game->pets[game->active];
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
        for (unsigned x = 0u; x < JELLI_WIDTH; ++x)
            s->pixels[y * s->stride + x] = in_round(x, y) ? BLACK : 0u;
    centered(s, page_title(ui->page), 233, 38, PALE);
    draw_pet_title(s, pet);
    sprite(s, asset_id(pet, phase), 185, 78, 3u);
    sprite(s, 3001u, 139, 118, 2u);
    sprite(s, pet->activity == JELLI_GIVING ? 3002u : 3004u, 282, 118, 2u);
    draw_companion_line(s, game, paused);
    draw_clock(s, pet);
    draw_inventory(s, game, ui);
    draw_needs(s, pet);
    draw_footer(s, game, ui, pet->asleep);
}

static JelliPetRenderKey render_key(const JelliGame *game, const JelliPetUi *ui, uint32_t phase,
                                    bool paused)
{
    const JelliPet *pet = &game->pets[game->active];
    uint8_t other_index = game->active == 0u ? 1u : 0u;
    const JelliPet *other = &game->pets[other_index];
    JelliPetRenderKey key = {0};
    uint64_t day_phase =
        (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) % JELLI_DAY_TICKS;
    key.phase = pet->activity == JELLI_IDLE && !pet->asleep ? phase % 2u : 0u;
    key.minute = (uint32_t)(day_phase / 600u);
    key.day =
        pet->ticks / JELLI_DAY_TICKS +
        (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) / JELLI_DAY_TICKS;
    key.active_id = pet->id;
    key.stored_id = other->id;
    key.result = ui->result;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        key.needs[i] = pet->needs[i];
    key.bond = pet->bond;
    key.food = game->food;
    key.gifts = game->gifts;
    key.active = game->active;
    key.count = game->count;
    key.page = (uint8_t)ui->page;
    key.save_status = ui->save_status;
    key.form = pet->form;
    key.location = pet->location;
    key.health = (uint8_t)pet->health;
    key.activity = (uint8_t)pet->activity;
    key.stored_form = other->form;
    key.bedtime = (uint8_t)pet->bedtime;
    key.asleep = pet->asleep;
    key.stored_asleep = other->asleep;
    key.reward_pending = pet->reward_pending;
    key.reward_claimed = pet->reward_claimed;
    key.time_unavailable = ui->time_unavailable;
    key.paused = paused;
    key.resuming = game->resuming;
    return key;
}

static bool same_frame_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return a->phase == b->phase && a->minute == b->minute && a->day == b->day &&
           a->active == b->active && a->count == b->count && a->page == b->page &&
           a->result == b->result && a->save_status == b->save_status &&
           a->time_unavailable == b->time_unavailable && a->paused == b->paused &&
           a->resuming == b->resuming;
}

static bool same_pet_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    if (a->active_id != b->active_id || a->stored_id != b->stored_id || a->bond != b->bond ||
        a->food != b->food || a->gifts != b->gifts || a->form != b->form ||
        a->location != b->location || a->health != b->health || a->activity != b->activity ||
        a->stored_form != b->stored_form || a->bedtime != b->bedtime || a->asleep != b->asleep ||
        a->stored_asleep != b->stored_asleep || a->reward_pending != b->reward_pending ||
        a->reward_claimed != b->reward_claimed)
        return false;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (a->needs[i] != b->needs[i])
            return false;
    }
    return true;
}

static bool same_render_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return same_frame_key(a, b) && same_pet_key(a, b);
}

void jelli_pet_render(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui,
                      uint32_t animation_ms, bool paused)
{
    if (surface == NULL || game == NULL || ui == NULL || surface->pixels == NULL ||
        surface->width < JELLI_WIDTH || surface->height < JELLI_HEIGHT ||
        surface->stride < surface->width || !jelli_game_valid(game))
        return;
    uint32_t phase = paused && ui->rendered ? ui->last_animation_phase : animation_ms / 450u;
    JelliPetRenderKey view = render_key(game, ui, phase, paused);
    if (ui->rendered && same_render_key(&view, &ui->last_view)) {
        surface->damage = (JelliRect){0};
        return;
    }
    surface->damage = (JelliRect){0u, 0u, JELLI_WIDTH, JELLI_HEIGHT};
    draw_scene(surface, game, ui, phase, paused);
    ui->last_view = view;
    ui->last_animation_phase = phase;
    ui->last_pet_ticks = game->pets[game->active].ticks;
    ui->last_revision = game->revision;
    ui->last_page = (uint8_t)ui->page;
    ui->rendered = true;
}

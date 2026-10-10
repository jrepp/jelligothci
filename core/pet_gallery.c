#include "jelli/sound.h"
#include "pet_gallery.h"
#include "game_internal.h"
#include "jelli/collection.h"

static const char *const names[JELLI_PRIZE_COUNT] = {
    "BUTTERFLY",  "PEARL TOOTH", "BREAKFAST SUN", "TEA SPRITE",    "MOVIE STAR",
    "BUBBLE GEM", "MOON CHARM",  "RAINBOW SEED",  "FRIENDSHIP BOW"};

bool jelli_pet_gallery_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button)
{
    if (!slot || slot > JELLI_PRIZE_COUNT)
        return false;
    if (ui->menu_open && ui->page == JELLI_UI_PRESENT_ACTION && slot <= 2u) {
        *button = (JelliPetUiButton){.bounds = {113u, slot == 1u ? 252u : 330u, 240u, 64u},
                                     .label = slot == 1u ? "GIVE" : "PUT AWAY",
                                     .scale = 2u};
        return true;
    }
    if (ui->menu_open && ui->page == JELLI_UI_COLLECTION) {
        unsigned index = slot - 1u;
        *button =
            (JelliPetUiButton){.bounds = {95u + index % 3u * 96u, 88u + index / 3u * 96u, 84u, 84u},
                               .label = names[index],
                               .icon = 11000u + slot,
                               .scale = 2u};
        return true;
    }
    if (!ui->menu_open && slot == 1u && (ui->catch_seen || ui->latched_prize)) {
        unsigned prize = ui->catch_seen ? ui->catch_seen : ui->latched_prize;
        *button = (JelliPetUiButton){.bounds = {193u, 70u, 80u, 80u},
                                     .label = ui->catch_seen ? "CATCH PRESENT" : "PRESENT",
                                     .icon = 11000u + prize,
                                     .scale = 2u};
        return true;
    }
    return false;
}

JelliResult jelli_pet_gallery_available(const JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (!slot || slot > JELLI_PRIZE_COUNT)
        return JELLI_INVALID_TARGET;
    if (ui->menu_open && ui->page == JELLI_UI_COLLECTION)
        return (game->prizes.owned & (1u << (slot - 1u))) ? JELLI_OK : JELLI_NO_ITEM;
    if (ui->page != JELLI_UI_PRESENT_ACTION || slot == 2u)
        return JELLI_OK;
    if (!ui->latched_prize || ui->latched_prize > JELLI_PRIZE_COUNT ||
        !(game->prizes.owned & (1u << (ui->latched_prize - 1u))))
        return JELLI_NO_ITEM;
    const JelliPet *pet = &game->pets[game->active];
    if (game->prizes.origin_pet[ui->latched_prize - 1u] == pet->id)
        return JELLI_NOT_READY;
    if (pet->asleep)
        return JELLI_ASLEEP;
    return pet->activity == JELLI_IDLE ? JELLI_OK : JELLI_BUSY;
}

static void saved(JelliPetUi *ui)
{
    ui->save_requested = true;
    ui->save_status = JELLI_SAVE_PENDING;
}

void jelli_pet_gallery_select(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    ui->result = jelli_pet_gallery_available(ui, game, slot);
    if (ui->result != JELLI_OK)
        return;
    if (ui->latched_prize == slot) {
        ui->present_return = JELLI_UI_COLLECTION;
        ui->page = JELLI_UI_PRESENT_ACTION;
        return;
    }
    ui->latched_prize = ui->highlighted_prize = (uint8_t)slot;
    ui->menu_open = false;
    ui->page = JELLI_UI_HOME;
    ui->sound_pending = true;
    ui->sound_cue = JELLI_SOUND_CONFIRM + 1u;
}

bool jelli_pet_gallery_tap(JelliPetUi *ui, JelliGame *game, int x, int y)
{
    if (ui->menu_open || (!game->prizes.offered && !ui->latched_prize) || x < 193 || x >= 273 ||
        y < 70 || y >= 150)
        return false;
    if (game->prizes.offered) {
        unsigned offered = game->prizes.offered;
        ui->result = jelli_prize_catch(game);
        if (ui->result == JELLI_OK) {
            ui->highlighted_prize = (uint8_t)offered;
            ui->catch_seen = 0u;
            ui->menu_open = true;
            ui->page = JELLI_UI_COLLECTION;
            jelli_game_emit(game, JELLI_EVENT_INPUT, 40u, JELLI_OK, offered,
                            &game->pets[game->active],
                            jelli_game_observe(game, &game->pets[game->active]));
        }
    } else {
        ui->present_return = JELLI_UI_HOME;
        ui->page = JELLI_UI_PRESENT_ACTION;
        ui->menu_open = true;
        ui->result = JELLI_OK;
        ui->sound_pending = true;
        ui->sound_cue = JELLI_SOUND_CONFIRM + 1u;
        return true;
    }
    if (ui->result == JELLI_OK) {
        saved(ui);
        ui->sound_pending = true;
        ui->sound_cue = JELLI_SOUND_PET + 1u;
        jelli_particles_burst_tuned(&ui->particles, x, y, true, 6u, 50u);
    }
    return true;
}

void jelli_pet_gallery_update(JelliPetUi *ui, const JelliGame *game, uint64_t time)
{
    if (ui->catch_seen != game->prizes.offered) {
        ui->catch_seen = game->prizes.offered;
        ui->catch_anchor_ms = time;
    }
    if (ui->latched_prize && !(game->prizes.owned & (1u << (ui->latched_prize - 1u))))
        ui->latched_prize = 0u;
}

void jelli_pet_gallery_key(const JelliPetUi *ui, const JelliGame *game, uint64_t time,
                           JelliPetRenderKey *key)
{
    key->prize_owned = game->prizes.owned;
    key->prize_discovered = game->prizes.discovered;
    key->offered_prize = game->prizes.offered;
    key->latched_prize = ui->latched_prize;
    key->highlighted_prize = ui->highlighted_prize;
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
        key->prize_origins[i] = game->prizes.origin_pet[i];
    if (time >= ui->catch_anchor_ms && !ui->menu_open && game->prizes.offered)
        key->catch_phase = (uint8_t)((time - ui->catch_anchor_ms) / 180u % 4u);
}

bool jelli_pet_gallery_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
        if (a->prize_origins[i] != b->prize_origins[i])
            return false;
    return a->prize_owned == b->prize_owned && a->prize_discovered == b->prize_discovered &&
           a->offered_prize == b->offered_prize && a->latched_prize == b->latched_prize &&
           a->highlighted_prize == b->highlighted_prize && a->catch_phase == b->catch_phase;
}

void jelli_pet_gallery_draw(Canvas *c, const JelliPetRenderKey *v)
{
    jelli_canvas_heading(c, "PRESENTS", 22, 3u);
    jelli_canvas_centered(c, "TAP A PRESENT TO HOLD", 61, 1u, MINT);
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i) {
        int x = 95 + (int)(i % 3u) * 96, y = 88 + (int)(i / 3u) * 96;
        bool owned = (v->prize_owned & (1u << i)) != 0u;
        bool selected = v->highlighted_prize == i + 1u;
        jelli_canvas_rect(c, x, y, 84, 84, selected ? GOLD : INK);
        jelli_canvas_rect(c, x + 3, y + 3, 78, 78, BG);
        c->dim = !owned;
        jelli_canvas_centered_sprite(c, 11001u + i, x + 42, y + 42, 2u);
        c->dim = false;
        if (owned)
            jelli_canvas_disk(c, x + 71, y + 71, 5, MINT);
    }
    if (v->highlighted_prize && v->highlighted_prize <= JELLI_PRIZE_COUNT)
        jelli_canvas_centered(c, names[v->highlighted_prize - 1u], 376, 1u, PALE);
}

void jelli_pet_gallery_draw_latched(Canvas *c, const JelliPetRenderKey *v)
{
    unsigned prize = v->offered_prize ? v->offered_prize : v->latched_prize;
    if (!prize || prize > JELLI_PRIZE_COUNT)
        return;
    static const int bob[] = {0, -3, -5, -3};
    int y = 110 + (v->offered_prize ? bob[v->catch_phase % 4u] : 0);
    jelli_canvas_disk(c, 233, y, 39, v->offered_prize ? GOLD : TEAL);
    jelli_canvas_disk(c, 233, y, 35, BG);
    jelli_canvas_centered_sprite(c, 11000u + prize, 233, y, 2u);
    jelli_canvas_centered(c, v->offered_prize ? "CATCH" : "PRESENT", 154, 1u, PALE);
}

void jelli_pet_gallery_action(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    if (slot == 2u) {
        ui->latched_prize = 0u;
        ui->highlighted_prize = 0u;
        ui->result = JELLI_OK;
    } else {
        ui->result = jelli_pet_gallery_available(ui, game, slot);
        if (ui->result != JELLI_OK)
            return;
        ui->result = jelli_prize_gift(game, ui->latched_prize - 1u);
        if (ui->result != JELLI_OK)
            return;
        ui->latched_prize = 0u;
        saved(ui);
    }
    ui->menu_open = false;
    ui->page = JELLI_UI_HOME;
}

void jelli_pet_gallery_draw_action(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    unsigned prize = ui->latched_prize;
    jelli_canvas_heading(c, "PRESENT", 28, 3u);
    if (prize && prize <= JELLI_PRIZE_COUNT) {
        jelli_canvas_centered(c, names[prize - 1u], 75, 2u, MINT);
        jelli_canvas_centered_sprite(c, 11000u + prize, 233, 148, 3u);
    }
    unsigned entry = game->pets[game->active].collection_entry;
    jelli_canvas_centered(c, jelli_collection_entries[entry - 1u].name, 205, 1u, PALE);
    JelliResult available = jelli_pet_gallery_available(ui, game, 1u);
    const char *reason = available == JELLI_NOT_READY ? "CHOOSE ANOTHER PET"
                         : available == JELLI_ASLEEP  ? "WAKE BEFORE GIVING"
                         : available == JELLI_BUSY    ? "PET IS BUSY"
                         : available == JELLI_NO_ITEM ? "PRESENT NO LONGER HELD"
                                                      : "GIVE TO THIS PET";
    jelli_canvas_centered(c, reason, 226, 1u, MINT);
    for (unsigned slot = 1u; slot <= 2u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_gallery_button(ui, slot, &b))
            continue;
        c->dim = slot == 1u && available != JELLI_OK;
        jelli_canvas_rect(c, (int)b.bounds.x, (int)b.bounds.y, (int)b.bounds.width, 64, TEAL);
        jelli_canvas_centered(c, b.label, (int)b.bounds.y + 20, 2u, PALE);
        c->dim = false;
    }
}

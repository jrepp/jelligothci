#include "jelli_asset_ids.h"
#include "jelli/sound.h"
#include "pet_gallery.h"
#include "pet_collectible_ui.h"
#include "pet_tune_library.h"
#include "game_internal.h"

bool jelli_pet_gallery_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button)
{
    if (ui->menu_open)
        return jelli_pet_collectible_button(ui, slot, button);
    if (slot != 1u || !ui->catch_seen || ui->catch_seen > jelli_prize_count)
        return false;
    *button = (JelliPetUiButton){.bounds = {193u, 70u, 80u, 80u},
                                 .label = "CATCH PRESENT",
                                 .icon = jelli_prizes[ui->catch_seen - 1u].asset,
                                 .scale = 2u};
    return true;
}

JelliResult jelli_pet_gallery_available(const JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    (void)ui;
    (void)game;
    return slot <= jelli_prize_count ? JELLI_OK : JELLI_INVALID_TARGET;
}

bool jelli_pet_gallery_tap(JelliPetUi *ui, JelliGame *game, int x, int y)
{
    if (ui->menu_open || !game->prizes.offered || x < 193 || x >= 273 || y < 70 || y >= 150)
        return false;
    unsigned offered = game->prizes.offered;
    ui->result = jelli_prize_catch(game);
    if (ui->result == JELLI_OK) {
        ui->latched_prize = ui->highlighted_prize = (uint8_t)offered;
        ui->catch_seen = 0u;
        ui->menu_open = true;
        ui->page = JELLI_UI_PRESENT_ACTION;
        ui->gift_category = offered <= JELLI_COLLECTIBLE_CHAIN_PRIZE        ? 0u
                            : offered == JELLI_COLLECTIBLE_CHAIN_PRIZE + 1u ? 1u
                                                                            : 2u;
        ui->gift_page = ui->gift_category ? 0u : (uint8_t)((offered - 1u) / 4u);
        ui->collectible_confirm = 0u;
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
        ui->sound_pending = true;
        ui->sound_cue = JELLI_SOUND_PET + 1u;
        jelli_particles_burst_tuned(&ui->particles, x, y, true, 6u, 50u);
        jelli_game_emit(game, JELLI_EVENT_INPUT, JELLI_INPUT_CATCH_PRESENT, JELLI_OK, offered,
                        &game->pets[game->active],
                        jelli_game_observe(game, &game->pets[game->active]));
    }
    return true;
}

void jelli_pet_gallery_update(JelliPetUi *ui, JelliGame *game, uint64_t time)
{
    jelli_pet_tune_update(ui, game, time);
    jelli_pet_collectible_launch_update(ui, game, time);
    bool winning = jelli_pet_collectible_win_update(ui, game, time);
    if (!winning && ui->page == JELLI_UI_COLLECTIBLE_PLAY && game->collectible.active &&
        game->collectible.paused)
        ui->collectible_menu = true;
    if (ui->catch_seen != game->prizes.offered) {
        ui->catch_seen = game->prizes.offered;
        ui->catch_anchor_ms = time;
    }
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
    return a->prize_owned == b->prize_owned && a->prize_discovered == b->prize_discovered &&
           a->offered_prize == b->offered_prize && a->latched_prize == b->latched_prize &&
           a->highlighted_prize == b->highlighted_prize && a->catch_phase == b->catch_phase;
}

void jelli_pet_gallery_draw_latched(Canvas *c, const JelliPetRenderKey *v)
{
    unsigned prize = v->offered_prize;
    if (!prize || prize > jelli_prize_count)
        return;
    static const int bob[] = {0, -3, -5, -3};
    int y = 110 + bob[v->catch_phase % 4u];
    jelli_canvas_disk(c, 233, y, 39, GOLD);
    jelli_canvas_disk(c, 233, y, 35, BG);
    jelli_canvas_centered_sprite(c, jelli_prizes[prize - 1u].asset, 233, y, 2u);
    jelli_canvas_centered(c, "CATCH", 154, 1u, PALE);
}

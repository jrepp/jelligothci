#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include <string.h>

void jelli_pet_collectible_key(const JelliPetUi *ui, const JelliGame *game, JelliPetRenderKey *key)
{
    key->collectible = game->collectible;
    key->tunes_seen = game->tunes_seen;
    key->tune_song = ui->tune.song;
    key->tune_elapsed_ms = ui->tune.elapsed_ms;
    key->tune_paused = ui->tune.paused;
    const JelliCollectibleProgress *progress = &game->pets[game->active].collectible_progress;
    key->collectible_special = progress->special_unlocked;
    unsigned cartridge =
        ui->latched_prize ? jelli_cartridge_index(ui->latched_prize - 1u) : JELLI_CARTRIDGE_COUNT;
    if (cartridge < JELLI_CARTRIDGE_COUNT) {
        key->cartridge_plays = game->cartridge_stats[cartridge].plays;
        key->cartridge_wins = game->cartridge_stats[cartridge].wins;
    }
    key->collectible_record_rewarded = progress->record_rewarded;
    if (ui->latched_prize && ui->latched_prize <= JELLI_PRIZE_COUNT)
        key->collectible_play_ticks = progress->play_ticks[ui->latched_prize - 1u];
    key->keyboard_focus = ui->keyboard_focus;
    key->keyboard_slot = ui->keyboard_slot;
    key->gift_category = ui->gift_category;
    key->gift_page = ui->gift_page;
    key->collectible_confirm = ui->collectible_confirm;
    key->collectible_reduced_motion = ui->collectible_reduced_motion;
    key->collectible_menu = ui->collectible_menu;
    key->collectible_win_ms = ui->collectible_win_ms;
    key->collectible_launch_ms = ui->collectible_launch_ms;
    key->collectible_launch_pending = ui->collectible_launch_pending;
    key->collectible_cheer_phase = (uint8_t)(game->pets[game->active].reaction_ticks / 2u % 2u);
}

static bool same_session(const JelliCollectibleSession *x, const JelliCollectibleSession *y)
{
    return x->pet_id == y->pet_id && x->prize == y->prize && x->mode == y->mode &&
           x->active == y->active && x->paused == y->paused &&
           x->record_cursor == y->record_cursor && x->elapsed_ticks == y->elapsed_ticks &&
           x->chain.length == y->chain.length && x->chain.pickup == y->chain.pickup &&
           jelli_bubble_same(&x->bubble, &y->bubble) && x->chain.status == y->chain.status &&
           x->chain.relaxed == y->chain.relaxed &&
           memcmp(x->chain.body, y->chain.body, sizeof(x->chain.body)) == 0;
}

static bool same_tunes(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return a->tunes_seen == b->tunes_seen && a->tune_song == b->tune_song &&
           a->tune_elapsed_ms == b->tune_elapsed_ms && a->tune_paused == b->tune_paused;
}

bool jelli_pet_collectible_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return same_tunes(a, b) && a->collectible_cheer_phase == b->collectible_cheer_phase &&
           a->cartridge_plays == b->cartridge_plays && a->cartridge_wins == b->cartridge_wins &&
           a->collectible_special == b->collectible_special &&
           a->collectible_record_rewarded == b->collectible_record_rewarded &&
           a->collectible_play_ticks == b->collectible_play_ticks &&
           a->keyboard_focus == b->keyboard_focus && a->keyboard_slot == b->keyboard_slot &&
           a->gift_category == b->gift_category && a->gift_page == b->gift_page &&
           a->collectible_confirm == b->collectible_confirm &&
           a->collectible_reduced_motion == b->collectible_reduced_motion &&
           a->collectible_menu == b->collectible_menu &&
           a->collectible_win_ms == b->collectible_win_ms &&
           a->collectible_launch_ms == b->collectible_launch_ms &&
           a->collectible_launch_pending == b->collectible_launch_pending &&
           same_session(&a->collectible, &b->collectible);
}

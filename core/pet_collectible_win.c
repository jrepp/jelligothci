#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include "jelli/creature.h"
#include "jelli/sound.h"
#include "jelli_asset_ids.h"

#define WIN_DURATION_MS 5000u
#define LOSS_DURATION_MS 2000u
#define FLOAT_DURATION_MS 900u

bool jelli_pet_collectible_lost(const JelliCollectibleSession *session)
{
    return (session->mode == JELLI_COLLECTIBLE_CHAIN &&
            session->chain.status == JELLI_CHAIN_LOST) ||
           (session->mode == JELLI_COLLECTIBLE_BUBBLE &&
            (session->bubble.status == JELLI_BUBBLE_TOP ||
             session->bubble.status == JELLI_BUBBLE_EMPTY));
}

bool jelli_pet_collectible_win_update(JelliPetUi *ui, JelliGame *game, uint64_t now)
{
    const JelliCollectibleSession *session = &game->collectible;
    bool winning =
        ui->page == JELLI_UI_COLLECTIBLE_PLAY && session->active &&
        ((session->mode == JELLI_COLLECTIBLE_CHAIN && session->chain.status == JELLI_CHAIN_WON) ||
         (session->mode == JELLI_COLLECTIBLE_BUBBLE && session->bubble.status == JELLI_BUBBLE_WON));
    bool losing = ui->page == JELLI_UI_COLLECTIBLE_PLAY && session->active &&
                  jelli_pet_collectible_lost(session);
    if (!winning && !losing) {
        ui->collectible_win_anchor = 0u;
        ui->collectible_win_ms = 0u;
        return false;
    }
    unsigned duration = losing ? LOSS_DURATION_MS : WIN_DURATION_MS;
    if (!ui->collectible_win_anchor && losing) {
        ui->sound_pending = true;
        ui->sound_played = false; /* Simulation outcomes bypass the menu-tap debounce. */
        ui->sound_cue = JELLI_SOUND_SLEEPY + 1u;
    }
    /* Anchor+1 distinguishes a first frame at time zero from an unstarted sequence. */
    if (!ui->collectible_win_anchor || now < ui->collectible_win_anchor - 1u)
        ui->collectible_win_anchor = now == UINT64_MAX ? UINT64_MAX : now + 1u;
    uint64_t elapsed = now - (ui->collectible_win_anchor - 1u);
    ui->collectible_win_ms = (uint16_t)(elapsed > duration ? duration : elapsed);
    ui->collectible_menu = false;
    if (elapsed < duration)
        return true;
    if (jelli_collectible_control(game, JELLI_COLLECTIBLE_CLOSE) != JELLI_OK)
        return true;
    ui->page = JELLI_UI_GIFT_SHELF;
    ui->gift_category = 1u;
    ui->gift_page = 0u;
    ui->collectible_win_anchor = 0u;
    ui->collectible_win_ms = 0u;
    ui->save_requested = true;
    return false;
}

static void stars(Canvas *c, unsigned elapsed, bool reduced)
{
    static const int8_t directions[12][2] = {{0, -100}, {50, -87}, {87, -50},  {100, 0},
                                             {87, 50},  {50, 87},  {0, 100},   {-50, 87},
                                             {-87, 50}, {-100, 0}, {-87, -50}, {-50, -87}};
    for (unsigned i = 0u; i < 24u; ++i) {
        unsigned phase = reduced ? 600u : (elapsed + i * 113u) % 1400u;
        int radius = reduced ? 165 + (int)(i / 12u) * 35 : 65 + (int)(phase * 160u / 1400u);
        unsigned angle = i % 12u;
        int x = 233 + directions[angle][0] * radius / 100;
        int y = 225 + directions[angle][1] * radius / 100;
        unsigned scale = i % 5u == 0u ? 2u : 1u;
        jelli_canvas_centered_sprite(c, JELLI_ASSET_EFFECTS_STAR, x, y, scale);
    }
}

void jelli_pet_collectible_win_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    unsigned elapsed = ui->collectible_win_ms;
    bool reduced = ui->collectible_reduced_motion;
    unsigned progress = reduced || elapsed >= FLOAT_DURATION_MS ? FLOAT_DURATION_MS : elapsed;
    /* Ease out with bounded integer arithmetic. */
    unsigned remaining = FLOAT_DURATION_MS - progress;
    unsigned eased = FLOAT_DURATION_MS - remaining * remaining / FLOAT_DURATION_MS;
    int x = 94 + (int)(139u * eased / FLOAT_DURATION_MS);
    int y = 363 - (int)(138u * eased / FLOAT_DURATION_MS);
    jelli_canvas_rect(c, 0, 0, JELLI_WIDTH, JELLI_HEIGHT, BG);
    jelli_canvas_disk(c, 233, 225, 114, TEAL);
    stars(c, elapsed, reduced);
    const JelliPet *pet = &game->pets[game->active];
    const JelliClip *clip = jelli_creature_clip(pet->form, JELLI_POSE_HAPPY);
    if (clip && clip->count) {
        unsigned scale = 3u + 2u * eased / FLOAT_DURATION_MS;
        unsigned frame = reduced ? 0u : elapsed / 180u % clip->count;
        jelli_canvas_centered_sprite(c, clip->frames[frame], x, y, scale);
    }
    jelli_canvas_centered(c, "STAR PLAYER!", 64, 2u, PALE);
    jelli_canvas_centered(c,
                          game->collectible.mode == JELLI_COLLECTIBLE_BUBBLE
                              ? "LAB COMPLETE - ALL 3 LEVELS!"
                              : "7 JELLIS - YOU DID IT!",
                          354, 1u, PALE);
    jelli_canvas_disk(c, 233, 462, 48, TEAL);
    jelli_canvas_centered(c, "BACK", 432, 2u, PALE);
}

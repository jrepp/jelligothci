#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include "jelli/sound.h"
#include <stdio.h>

#define LAUNCH_DURATION_MS 3000u
#define INSERT_START_MS 1200u
#define INSERT_DURATION_MS 1500u

void jelli_pet_collectible_launch(JelliPetUi *ui, const JelliGame *game)
{
    if (ui->collectible_launch_pending || !ui->latched_prize ||
        jelli_cartridge_index(ui->latched_prize - 1u) >= JELLI_CARTRIDGE_COUNT)
        return;
    ui->result = jelli_collectible_available(game, ui->latched_prize - 1u);
    if (ui->result != JELLI_OK)
        return;
    ui->collectible_launch_pet = game->pets[game->active].id;
    ui->collectible_launch_ms = 0u;
    ui->collectible_launch_anchor = 0u;
    ui->collectible_launch_pending = true;
}

void jelli_pet_collectible_launch_update(JelliPetUi *ui, JelliGame *game, uint64_t now)
{
    if (!ui->collectible_launch_pending)
        return;
    if (!ui->menu_open || ui->page != JELLI_UI_PRESENT_ACTION ||
        ui->collectible_launch_pet != game->pets[game->active].id) {
        ui->collectible_launch_pending = false;
        ui->collectible_launch_anchor = 0u;
        ui->collectible_launch_ms = 0u;
        return;
    }
    /* Anchor+1 permits an initial frame at zero and handles clock reset. */
    if (!ui->collectible_launch_anchor || now < ui->collectible_launch_anchor - 1u)
        ui->collectible_launch_anchor = now == UINT64_MAX ? UINT64_MAX : now + 1u;
    uint64_t elapsed = now - (ui->collectible_launch_anchor - 1u);
    if (ui->collectible_launch_ms < INSERT_START_MS + INSERT_DURATION_MS &&
        elapsed >= INSERT_START_MS + INSERT_DURATION_MS) {
        ui->sound_pending = true;
        ui->sound_cue = JELLI_SOUND_CONFIRM + 1u;
    }
    ui->collectible_launch_ms =
        (uint16_t)(elapsed > LAUNCH_DURATION_MS ? LAUNCH_DURATION_MS : elapsed);
    if (elapsed < LAUNCH_DURATION_MS)
        return;
    /* No engine state changes until insertion completes; Back cancels safely. */
    ui->collectible_launch_pending = false;
    ui->collectible_launch_anchor = 0u;
    ui->collectible_launch_ms = 0u;
    jelli_pet_collectible_open(ui, game);
}

static const char *launch_message(const JelliPetUi *ui)
{
    if (ui->collectible_launch_pending)
        return ui->collectible_launch_ms < INSERT_START_MS ? "READY TO PLAY" : "INSERTING...";
    if (ui->result == JELLI_ASLEEP)
        return "WAKE YOUR PET TO PLAY";
    if (ui->result == JELLI_BUSY)
        return "FINISH CARE THEN PLAY";
    return ui->collectible_confirm == 2u ? "REPLACES OTHER SAVED PLAY" : "";
}

void jelli_pet_cartridge_draw(Canvas *canvas, const JelliPetUi *ui, const JelliGame *game,
                              unsigned prize)
{
    unsigned index = jelli_cartridge_index(prize);
    if (index >= JELLI_CARTRIDGE_COUNT)
        return;
    const char *title = prize == JELLI_COLLECTIBLE_CHAIN_PRIZE ? "JELLI CHAIN" : "BUBBLE LAB";
    jelli_canvas_centered(canvas, "CARTRIDGE", 53, 1u, MINT);
    jelli_canvas_heading(canvas, title, 85, 4u);
    unsigned elapsed = ui->collectible_launch_ms;
    unsigned progress = elapsed > INSERT_START_MS ? elapsed - INSERT_START_MS : 0u;
    if (progress > INSERT_DURATION_MS)
        progress = INSERT_DURATION_MS;
    if (ui->collectible_reduced_motion)
        progress = 0u; /* Keep the cartridge still; seated status communicates completion. */
    int y = 212 + (int)(progress * 84u / INSERT_DURATION_MS);
    if (ui->collectible_launch_pending) {
        jelli_canvas_rect(canvas, 123, 282, 220, 31, TEAL);
        jelli_canvas_rect(canvas, 139, 289, 188, 10, 0u);
    }
    Canvas cartridge = *canvas;
    if (ui->collectible_launch_pending && cartridge.bottom > 299)
        cartridge.bottom = 299;
    jelli_canvas_centered_sprite(&cartridge, jelli_prizes[prize].asset, 233, y, 4u);
    /* The front of the slot occludes the inserted cartridge using the portable canvas. */
    if (ui->collectible_launch_pending)
        jelli_canvas_rect(canvas, 123, 299, 220, 21, TEAL);
    if (elapsed >= INSERT_START_MS + INSERT_DURATION_MS) {
        jelli_canvas_rect(canvas, 153, 306, 160, 4, MINT);
        jelli_canvas_centered(canvas, "CLICK!", 365, 1u, MINT);
    } else
        jelli_canvas_centered(canvas, launch_message(ui),
                              ui->collectible_launch_pending ? 365 : 329, 1u, GOLD);
    char line[40];
    int n = snprintf(line, sizeof(line), "PLAYS %u   WINS %u", game->cartridge_stats[index].plays,
                     game->cartridge_stats[index].wins);
    if (n > 0 && (size_t)n < sizeof(line))
        jelli_canvas_centered(canvas, line, ui->collectible_launch_pending ? 333 : 305, 2u, PALE);
}

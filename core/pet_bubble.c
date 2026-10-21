#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include "jelli_asset_ids.h"
#include <stdio.h>

bool jelli_pet_bubble_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button)
{
    static const char *const assists[] = {"NORMAL", "RELAX", "MANUAL"};
    if (slot != 6u)
        return false;
    unsigned assist = ui->last_view.collectible.bubble.assist;
    *button = (JelliPetUiButton){.bounds = {325u, 271u, 68u, 52u},
                                 .scale = 1u,
                                 .label = assists[assist <= JELLI_BUBBLE_MANUAL ? assist : 0u]};
    return true;
}

void jelli_pet_bubble_action(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    static const unsigned controls[] = {JELLI_COLLECTIBLE_LEFT, JELLI_COLLECTIBLE_UP,
                                        JELLI_COLLECTIBLE_RIGHT, JELLI_COLLECTIBLE_DOWN};
    if (slot < 2u || slot > 6u)
        return;
    unsigned control = slot == 6u
                           ? JELLI_COLLECTIBLE_NORMAL + (game->collectible.bubble.assist + 1u) % 3u
                           : controls[slot - 2u];
    ui->result = jelli_collectible_control(game, control);
    if (ui->result == JELLI_OK) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
        ui->rendered = false;
    }
}

static void cell_draw(Canvas *c, unsigned cell, int x, int y)
{
    static const uint16_t colors[] = {0u, 0xfbaeu, 0x4e9fu, 0xff2cu};
    static const char *const symbols[] = {"", "+", "O", "X"};
    unsigned color = cell & 3u;
    if (!color)
        return;
    if (cell >= 5u) {
        /* Four spikes and dark outline distinguish fixed viruses from smooth medicine. */
        jelli_canvas_rect(c, x - 3, y - 12, 6, 24, colors[color]);
        jelli_canvas_rect(c, x - 12, y - 3, 24, 6, colors[color]);
        jelli_canvas_disk(c, x, y, 10, INK);
        jelli_canvas_disk(c, x, y, 8, colors[color]);
    } else {
        jelli_canvas_disk(c, x, y, 10, colors[color]);
        jelli_canvas_rect(c, x - 6, y - 8, 5, 2, PALE);
    }
    jelli_canvas_text(c, symbols[color], x - 4, y - 4, 1u, BG);
}

void jelli_pet_bubble_draw(Canvas *c, const JelliCollectibleSession *session)
{
    const JelliBubble *lab = &session->bubble;
    char text[40];
    int n = snprintf(text, sizeof(text), "SCORE %u", lab->score);
    if (n > 0 && (size_t)n < sizeof(text))
        jelli_canvas_centered(c, text, 29, 2u, PALE);
    n = snprintf(text, sizeof(text), "LAB %u/3   VIRUSES %u", lab->level + 1u,
                 jelli_bubble_viruses(lab));
    if (n > 0 && (size_t)n < sizeof(text))
        jelli_canvas_centered(c, text, 61, 1u, MINT);
    jelli_canvas_centered_sprite(c, JELLI_ASSET_HEALTH_MEDICINE, 101, 176, 2u);
    jelli_canvas_centered_sprite(c, JELLI_ASSET_HEALTH_SHOT, 366, 205, 2u);
    jelli_canvas_rect(c, 152, 84, 162, 266, TEAL);
    for (unsigned i = 0u; i < JELLI_BUBBLE_CELLS; ++i) {
        int x = 168 + (int)(i % JELLI_BUBBLE_WIDTH) * 26;
        int y = 100 + (int)(i / JELLI_BUBBLE_WIDTH) * 26;
        jelli_canvas_rect(c, x - 13, y - 13, 25, 25, i < JELLI_BUBBLE_WIDTH ? 0x49a8u : BG);
        cell_draw(c, lab->cells[i], x, y);
    }
    if (lab->status == JELLI_BUBBLE_RUNNING) {
        int x = 168 + (int)lab->x * 26, y = 100 + (int)lab->y * 26;
        cell_draw(c, lab->color, x, y);
        cell_draw(c, lab->color, x + (lab->vertical ? 0 : 26), y + (lab->vertical ? 26 : 0));
    }
    jelli_canvas_text(c, "NEXT", 337, 109, 1u, MINT);
    cell_draw(c, lab->cursor % 3u + 1u, 348, 142);
    cell_draw(c, lab->cursor % 3u + 1u, 374, 142);
    n = snprintf(text, sizeof(text), "%u LEFT", JELLI_BUBBLE_PAIRS - lab->cursor);
    if (n > 0 && (size_t)n < sizeof(text))
        jelli_canvas_text(c, text, 330, 331, 1u, PALE);
    jelli_canvas_text(c, "MATCH 4", 77, 238, 1u, PALE);
    jelli_canvas_text(c, "SAME", 89, 254, 1u, PALE);
    jelli_canvas_text(c, "SYMBOL", 81, 270, 1u, PALE);
    jelli_canvas_centered(c, "SWIPE MOVE / DOWN DROP", 371, 1u, MINT);
    jelli_canvas_centered(c, "TAP WELL TO ROTATE", 393, 1u, MINT);
}

void jelli_pet_bubble_checkpoint(JelliPetUi *ui, const JelliCollectibleSession *before,
                                 const JelliCollectibleSession *after)
{
    if (after->mode != JELLI_COLLECTIBLE_BUBBLE || !after->pet_id)
        return;
    if (before->bubble.cursor != after->bubble.cursor ||
        before->bubble.level != after->bubble.level ||
        before->bubble.status != after->bubble.status || before->paused != after->paused) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

void jelli_pet_cartridge_draw(Canvas *canvas, const JelliGame *game, unsigned prize)
{
    unsigned index = jelli_cartridge_index(prize);
    if (index >= JELLI_CARTRIDGE_COUNT)
        return;
    jelli_canvas_heading(canvas, jelli_prizes[prize].name, 52, 2u);
    jelli_canvas_centered_sprite(canvas, jelli_prizes[prize].asset, 233, 202, 4u);
    char line[40];
    int n = snprintf(line, sizeof(line), "PLAYS %u   WINS %u", game->cartridge_stats[index].plays,
                     game->cartridge_stats[index].wins);
    if (n > 0 && (size_t)n < sizeof(line))
        jelli_canvas_centered(canvas, line, 305, 2u, PALE);
}

#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include "jelli_asset_ids.h"
#include "jelli/sound.h"
#include <stdio.h>

bool jelli_pet_bubble_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button)
{
    static const char *const assists[] = {"NORMAL", "RELAX", "MANUAL"};
    if (slot != 6u)
        return false;
    unsigned assist = ui->last_view.collectible.bubble.assist;
    *button = (JelliPetUiButton){.bounds = {374u, 267u, 68u, 52u},
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
        /* Spikes, angry brows and teeth distinguish viruses without relying on color. */
        jelli_canvas_rect(c, x - 3, y - 15, 6, 30, colors[color]);
        jelli_canvas_rect(c, x - 15, y - 3, 30, 6, colors[color]);
        jelli_canvas_disk(c, x, y, 13, INK);
        jelli_canvas_disk(c, x, y, 11, colors[color]);
        jelli_canvas_rect(c, x - 8, y - 6, 5, 4, PALE);
        jelli_canvas_rect(c, x + 3, y - 6, 5, 4, PALE);
        jelli_canvas_rect(c, x - 5, y - 5, 2, 3, BG);
        jelli_canvas_rect(c, x + 3, y - 5, 2, 3, BG);
        jelli_canvas_rect(c, x - 9, y - 9, 4, 2, BG);
        jelli_canvas_rect(c, x - 6, y - 8, 4, 2, BG);
        jelli_canvas_rect(c, x + 5, y - 9, 4, 2, BG);
        jelli_canvas_rect(c, x + 2, y - 8, 4, 2, BG);
        jelli_canvas_rect(c, x - 8, y + 3, 3, 5, PALE);
        jelli_canvas_rect(c, x + 5, y + 3, 3, 5, PALE);
        jelli_canvas_text(c, symbols[color], x - 3, y + 1, 1u, BG);
    } else {
        jelli_canvas_disk(c, x, y, 13, colors[color]);
        jelli_canvas_rect(c, x - 8, y - 10, 6, 2, PALE);
        jelli_canvas_text(c, symbols[color], x - 3, y - 4, 1u, BG);
    }
}

static void counter(Canvas *c, const char *label, unsigned value, int y, unsigned scale)
{
    char text[12];
    jelli_canvas_text(c, label, 62, y, 2u, MINT);
    int n = snprintf(text, sizeof(text), "%u", value);
    if (n > 0 && (size_t)n < sizeof(text))
        jelli_canvas_text(c, text, 62, y + 21, scale, PALE);
}

void jelli_pet_bubble_draw(Canvas *c, const JelliCollectibleSession *session)
{
    const JelliBubble *lab = &session->bubble;
    counter(c, "SCORE", lab->score, 112, lab->score < 10000u ? 3u : 2u);
    counter(c, "LEVEL", lab->level + 1u, 179, 3u);
    counter(c, "VIRUS", jelli_bubble_viruses(lab), 246, 3u);
    jelli_canvas_rect(c, 171, 47, 198, 326, TEAL);
    for (unsigned i = 0u; i < JELLI_BUBBLE_CELLS; ++i) {
        int x = 190 + (int)(i % JELLI_BUBBLE_WIDTH) * 32;
        int y = 66 + (int)(i / JELLI_BUBBLE_WIDTH) * 32;
        jelli_canvas_rect(c, x - 16, y - 16, 31, 31, i < JELLI_BUBBLE_WIDTH ? 0x49a8u : BG);
        cell_draw(c, lab->cells[i], x, y);
    }
    if (lab->status == JELLI_BUBBLE_RUNNING) {
        int x = 190 + (int)lab->x * 32, y = 66 + (int)lab->y * 32;
        cell_draw(c, lab->color, x, y);
        cell_draw(c, lab->color, x + (lab->vertical ? 0 : 32), y + (lab->vertical ? 32 : 0));
    }
    jelli_canvas_text(c, "NEXT", 386, 150, 1u, MINT);
    cell_draw(c, lab->cursor % 3u + 1u, 389, 181);
    cell_draw(c, lab->cursor % 3u + 1u, 421, 181);
}

static void bubble_feedback(JelliPetUi *ui, const JelliCollectibleSession *before,
                            const JelliCollectibleSession *after)
{
    if (ui->page == JELLI_UI_COLLECTIBLE_PLAY && before->active && !before->paused &&
        after->active && before->mode == JELLI_COLLECTIBLE_BUBBLE &&
        before->pet_id == after->pet_id && before->generation == after->generation &&
        before->bubble.status == JELLI_BUBBLE_RUNNING) {
        unsigned cue = JELLI_SOUND_COUNT;
        if (after->bubble.level > before->bubble.level ||
            jelli_bubble_viruses(&after->bubble) < jelli_bubble_viruses(&before->bubble))
            cue = JELLI_SOUND_SPARKLE;
        else if (after->bubble.cursor != before->bubble.cursor)
            cue = JELLI_SOUND_CONFIRM;
        else if (before->bubble.input_count && (after->bubble.x != before->bubble.x ||
                                                after->bubble.vertical != before->bubble.vertical))
            cue = JELLI_SOUND_BACK;
        if (cue != JELLI_SOUND_COUNT) {
            ui->sound_pending = true;
            ui->sound_played = false; /* Simulation outcomes bypass the menu-tap debounce. */
            ui->sound_cue = (uint8_t)(cue + 1u);
        }
    }
}

void jelli_pet_bubble_checkpoint(JelliPetUi *ui, const JelliCollectibleSession *before,
                                 const JelliCollectibleSession *after)
{
    if (after->mode != JELLI_COLLECTIBLE_BUBBLE || !after->pet_id)
        return;
    bubble_feedback(ui, before, after);
    if (before->bubble.cursor != after->bubble.cursor ||
        before->bubble.level != after->bubble.level ||
        before->bubble.status != after->bubble.status || before->paused != after->paused) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

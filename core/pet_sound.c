#include "pet_sound.h"
#include <stdio.h>

/* Ten segments fit inside the Settings center disk (radius 70 around 233,215):
 * at the bar's bottom row the disk is 110 pixels wide; the bar is 107. */
enum {
    BAR_SEGMENT = 8,
    BAR_GAP = 3,
    BAR_WIDTH = (int)JELLI_SOUND_LEVEL_MAX * (BAR_SEGMENT + BAR_GAP) - BAR_GAP,
    BAR_LEFT = 233 - BAR_WIDTH / 2,
    BAR_TOP = 244,
    BAR_HEIGHT = 14
};

bool jelli_pet_sound_slot(const JelliPetUi *ui, unsigned slot)
{
    return ui->page == JELLI_UI_SETTINGS && !ui->clock_edit && slot != 0u &&
           (ui->sound_edit || slot == JELLI_UI_SOUND_SLOT);
}

bool jelli_pet_sound_button(unsigned slot, JelliPetUiButton *button)
{
    static const char *const labels[] = {"VOL -", "VOL +"};
    static const unsigned centers[2][2] = {{61u, 233u}, {405u, 233u}};
    if (!button || !slot || slot > 2u)
        return false;
    *button = (JelliPetUiButton){
        .bounds = {centers[slot - 1u][0] - 48u, centers[slot - 1u][1] - 48u, 96u, 96u},
        .label = labels[slot - 1u],
        .circular = true};
    return true;
}

JelliResult jelli_pet_sound_available(const JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (!ui->sound_edit)
        return JELLI_OK;
    if ((slot == 1u && game->sound_level == 0u) ||
        (slot == 2u && game->sound_level >= JELLI_SOUND_LEVEL_MAX))
        return JELLI_NOT_READY;
    return slot == 1u || slot == 2u ? JELLI_OK : JELLI_INVALID_TARGET;
}

void jelli_pet_sound_action(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    if (!ui->sound_edit) {
        ui->sound_edit = true;
        ui->result = JELLI_OK;
        return;
    }
    ui->result = jelli_pet_sound_available(ui, game, slot);
    if (ui->result != JELLI_OK)
        return;
    game->sound_level = (uint8_t)(slot == 1u ? game->sound_level - 1u : game->sound_level + 1u);
    ui->save_requested = true;
    ui->save_status = JELLI_SAVE_PENDING;
}

void jelli_pet_sound_draw(Canvas *canvas, const JelliPetRenderKey *view)
{
    unsigned level =
        view->sound_level > JELLI_SOUND_LEVEL_MAX ? JELLI_SOUND_LEVEL_MAX : view->sound_level;
    jelli_canvas_disk(canvas, 233, 215, 74, 0x738eu);
    jelli_canvas_disk(canvas, 233, 215, 70, BG);
    char value[4];
    int size = snprintf(value, sizeof(value), "%u", level);
    if (size > 0 && (size_t)size < sizeof(value))
        jelli_canvas_centered(canvas, value, 162, 4u, 0xffffu);
    jelli_canvas_centered(canvas, "SOUND", 214, 2u, 0xffffu);
    for (unsigned i = 0u; i < JELLI_SOUND_LEVEL_MAX; ++i) {
        int x = BAR_LEFT + (int)i * (BAR_SEGMENT + BAR_GAP);
        if (i < level) {
            jelli_canvas_rect(canvas, x, BAR_TOP, BAR_SEGMENT, BAR_HEIGHT, MINT);
            continue;
        }
        jelli_canvas_rect(canvas, x, BAR_TOP, BAR_SEGMENT, BAR_HEIGHT, 0x738eu);
        jelli_canvas_rect(canvas, x + 2, BAR_TOP + 2, BAR_SEGMENT - 4, BAR_HEIGHT - 4, BG);
    }
}

#include "pet_food.h"
#include "jelli/nutrition.h"
#include <string.h>

bool jelli_pet_food_button(unsigned slot, JelliPetUiButton *button)
{
    if (!slot || slot > jelli_food_count)
        return false;
    unsigned i = slot - 1u;
    *button = (JelliPetUiButton){.bounds = {95u + i % 3u * 96u, 88u + i / 3u * 96u, 84u, 84u},
                                 .label = jelli_foods[i].name,
                                 .icon = jelli_foods[i].icon,
                                 .scale = 2u};
    return true;
}

JelliResult jelli_pet_food_available(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (!slot || slot > jelli_food_count)
        return JELLI_INVALID_TARGET;
    return jelli_game_check(game,
                            (JelliCommand){JELLI_CMD_FEED, game->pets[game->active].id, slot - 1u},
                            &ui->action_scratch);
}

void jelli_pet_food_select(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    ui->result = jelli_game_command(
        game, (JelliCommand){JELLI_CMD_FEED, game->pets[game->active].id, slot - 1u});
    if (ui->result == JELLI_OK) {
        ui->page = JELLI_UI_HOME;
        ui->menu_open = false;
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

void jelli_pet_food_draw(Canvas *c, const JelliPetUi *ui)
{
    jelli_canvas_heading(c, "FOOD", 22, 3u);
    jelli_canvas_centered(c, "ONE FOOD EACH", 61, 1u, MINT);
    for (unsigned slot = 1u; slot <= jelli_food_count; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_food_button(slot, &b))
            continue;
        int x = (int)b.bounds.x, y = (int)b.bounds.y;
        c->dim = (ui->last_view.unavailable & (1u << slot)) != 0u;
        jelli_canvas_rect(c, x, y, 84, 84, INK);
        jelli_canvas_rect(c, x + 3, y + 3, 78, 78, BG);
        jelli_canvas_centered_sprite(c, b.icon, x + 42, y + 35, 2u);
        jelli_canvas_text(c, b.label, x + 42 - (int)strlen(b.label) * 4, y + 66, 1u, PALE);
        c->dim = false;
    }
    if (jelli_food_count <= 3u) {
        jelli_canvas_centered(c, "MEAL: MORE FULLNESS", 225, 1u, PALE);
        jelli_canvas_centered(c, "FRUIT: LIGHT AND JUICY", 255, 1u, MINT);
        jelli_canvas_centered(c, "SOUP: FOOD AND WATER", 285, 1u, PALE);
    }
}

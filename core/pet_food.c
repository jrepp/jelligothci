#include "pet_food.h"
#include "jelli/nutrition.h"
#include <stdio.h>
#include <string.h>

/* The ninth grid cell restocks the shared food inventory. */
bool jelli_pet_food_button(unsigned slot, JelliPetUiButton *button)
{
    if (!slot || (slot > jelli_food_count && slot != 9u))
        return false;
    unsigned i = slot - 1u;
    *button = (JelliPetUiButton){.bounds = {95u + i % 3u * 96u, 88u + i / 3u * 96u, 84u, 84u},
                                 .label = slot == 9u ? "GET FOOD" : jelli_foods[i].name,
                                 .icon = slot == 9u ? 6001u : jelli_foods[i].icon,
                                 .scale = 2u};
    return true;
}

static JelliCommand food_command(const JelliGame *game, unsigned slot)
{
    return (JelliCommand){slot == 9u ? JELLI_CMD_REFILL_FOOD : JELLI_CMD_FEED,
                          game->pets[game->active].id, slot - 1u};
}

JelliResult jelli_pet_food_available(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (!slot || (slot > jelli_food_count && slot != 9u))
        return JELLI_INVALID_TARGET;
    return jelli_game_check(game, food_command(game, slot), &ui->action_scratch);
}

void jelli_pet_food_select(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    ui->result = jelli_game_command(game, food_command(game, slot));
    if (ui->result == JELLI_OK) {
        if (slot != 9u) {
            ui->page = JELLI_UI_HOME;
            ui->menu_open = false;
        }
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

static void draw_food_button(Canvas *c, const JelliPetUi *ui, unsigned slot)
{
    JelliPetUiButton b;
    if (!jelli_pet_food_button(slot, &b))
        return;
    int x = (int)b.bounds.x, y = (int)b.bounds.y;
    bool disabled = (ui->last_view.unavailable & (1u << slot)) != 0u;
    jelli_canvas_rect(c, x, y, 84, 84, disabled ? INK : TEAL);
    jelli_canvas_rect(c, x + 3, y + 3, 78, 78, BG);
    c->dim = disabled;
    jelli_canvas_centered_sprite(c, b.icon, x + 42, y + 27, 2u);
    c->dim = false; /* Keep disabled labels readable; only the icon/border dims. */
    if (slot == 9u) {
        jelli_canvas_text(c, "GET", x + 18, y + 34, 2u, PALE);
        jelli_canvas_text(c, "FOOD", x + 10, y + 58, 2u, PALE);
        return;
    }
    unsigned scale = strlen(b.label) <= 5u ? 2u : 1u;
    jelli_canvas_text(c, b.label, x + 42 - (int)strlen(b.label) * 4 * (int)scale, y + 56, scale,
                      PALE);
}

void jelli_pet_food_draw(Canvas *c, const JelliPetUi *ui)
{
    jelli_canvas_heading(c, "FOOD", 22, 3u);
    char count[20];
    int length = snprintf(count, sizeof(count), "FOOD LEFT %u", (unsigned)ui->last_view.food);
    jelli_canvas_rect(c, 97, 52, 272, 30, BG);
    if (length > 0 && (size_t)length < sizeof(count))
        jelli_canvas_centered(c, count, 55, 2u, PALE);
    for (unsigned slot = 1u; slot <= 9u; ++slot)
        draw_food_button(c, ui, slot);
    if (jelli_food_count <= 3u) {
        jelli_canvas_rect(c, 65, 184, 336, 88, BG);
        jelli_canvas_centered(c, "MEAL: FILLING", 188, 2u, PALE);
        jelli_canvas_centered(c, "FRUIT: JUICY", 215, 2u, PALE);
        jelli_canvas_centered(c, "SOUP: HYDRATES", 242, 2u, PALE);
        jelli_canvas_rect(c, 95, 283, 182, 81, BG);
        const char *first = !ui->last_view.food      ? "GET FOOD"
                            : ui->last_view.asleep   ? "WAKE PET"
                            : ui->last_view.activity ? "PET BUSY"
                                                     : "ONE FOOD";
        const char *second = !ui->last_view.food      ? "FOR FIVE"
                             : ui->last_view.asleep   ? "TO FEED"
                             : ui->last_view.activity ? "WAIT"
                                                      : "PER MEAL";
        jelli_canvas_text(c, first, 102, 289, 2u, PALE);
        jelli_canvas_text(c, second, 102, 321, 2u, MINT);
    }
}

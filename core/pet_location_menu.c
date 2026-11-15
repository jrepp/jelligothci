#include "jelli/pet_ui.h"
#include "jelli/locations.h"
#include "jelli_asset_ids.h"

bool jelli_pet_location_active(const JelliPetUi *ui, unsigned slot)
{
    return ui->menu_open && ui->location_menu && ui->page == JELLI_UI_MOMENTS && slot;
}

bool jelli_pet_location_open(JelliPetUi *ui, const JelliGame *game, int x, int y)
{
    if (ui->menu_open || x < 100 || x >= 366 || y < 50 || y >= 72)
        return false;
    ui->location_pet_id = game->pets[game->active].id;
    ui->location_menu = true;
    ui->menu_open = true;
    ui->page = JELLI_UI_MOMENTS;
    ui->activity_count = (uint8_t)jelli_location_count;
    ui->activity_page = 0u;
    ui->activity_snapshot = true;
    ui->discovery_pet = ui->location_pet_id;
    ui->result = JELLI_OK;
    return true;
}

JelliResult jelli_pet_location_available(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (!slot || slot > jelli_location_count || ui->location_pet_id != game->pets[game->active].id)
        return JELLI_INVALID_TARGET;
    JelliCommand command = {JELLI_CMD_TRAVEL, ui->location_pet_id, slot - 1u};
    return jelli_game_check(game, command, &ui->action_scratch);
}

bool jelli_pet_location_button(const JelliPetUi *ui, unsigned slot, bool asleep,
                               JelliPetUiButton *button)
{
    (void)ui;
    if (!slot || slot > jelli_location_count ||
        !jelli_pet_ui_button(JELLI_UI_MOMENTS, slot, asleep, true, button))
        return false;
    button->label = jelli_locations[slot - 1u].name;
    button->icon = JELLI_ASSET_MENUS_OUTING;
    return true;
}

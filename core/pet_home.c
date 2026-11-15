#include "jelli/pet_ui.h"
#include "jelli/activities.h"

unsigned jelli_pet_home_choice(const JelliPetUi *ui, unsigned slot)
{
    return ui->home_snapshot && (slot == 1u || slot == 6u) ? ui->home_choices[slot == 1u ? 0u : 1u]
                                                           : jelli_moment_count + 2u;
}

void jelli_pet_home_refresh(JelliPetUi *ui, const JelliGame *game)
{
    if (!ui->menu_open || ui->page != JELLI_UI_HOME ||
        ui->discovery_pet != game->pets[game->active].id)
        ui->home_snapshot = false;
    if (!ui->menu_open || ui->page != JELLI_UI_HOME || ui->home_snapshot)
        return;
    ui->home_choices[0] = ui->home_choices[1] = (uint8_t)(jelli_moment_count + 2u);
    ui->home_offers = ui->ready_offers;
    unsigned count = 0u;
    for (unsigned group = 0u; group < 2u && count < 2u; ++group) {
        for (unsigned id = 0u; id < jelli_moment_count + 2u && count < 2u; ++id) {
            bool offered = id < jelli_moment_count && (ui->ready_offers & (UINT32_C(1) << id));
            if (offered != (group == 0u))
                continue;
            if (jelli_game_check(game, jelli_pet_activity_command(game, id), &ui->action_scratch) ==
                JELLI_OK)
                ui->home_choices[count++] = (uint8_t)id;
        }
    }
    ui->home_snapshot = true;
}

bool jelli_pet_home_button(const JelliPetUi *ui, unsigned slot, bool asleep,
                           JelliPetUiButton *button)
{
    unsigned id = jelli_pet_home_choice(ui, slot);
    if (!jelli_pet_activity_button(id / 5u, id % 5u + 1u, asleep, button))
        return false;
    button->bounds =
        slot == 1u ? (JelliRect){62u, 63u, 96u, 96u} : (JelliRect){13u, 185u, 96u, 96u};
    return true;
}

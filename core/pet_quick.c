#include "jelli/pet_ui.h"
#include "jelli/activities.h"

/* IDs 0..31 are moments; 128+ encodes a direct UI action. */
enum { QUICK_ACTION = 128, QUICK_LIMIT = 4 };

bool jelli_pet_quick_active(const JelliPetUi *ui, unsigned slot)
{
    return ui->menu_open && ui->quick_menu && ui->page == JELLI_UI_MOMENTS && slot;
}

unsigned jelli_pet_quick_choice(const JelliPetUi *ui, unsigned slot)
{
    return slot && slot <= ui->quick_count ? ui->quick_choices[slot - 1u] : UINT8_MAX;
}

static JelliResult available(JelliPetUi *ui, const JelliGame *game, unsigned choice)
{
    JelliCommand command;
    if (choice >= QUICK_ACTION) {
        unsigned action = choice - QUICK_ACTION;
        if (action >= JELLI_UI_ACTION_COUNT ||
            !jelli_pet_ui_command(ui, game, (JelliPetUiAction)action, &command))
            return JELLI_INVALID_TARGET;
    } else {
        command = jelli_pet_activity_command(game, choice);
    }
    return jelli_game_check(game, command, &ui->action_scratch);
}

JelliResult jelli_pet_quick_available(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (ui->quick_pet_id != game->pets[game->active].id)
        return JELLI_INVALID_TARGET;
    return available(ui, game, jelli_pet_quick_choice(ui, slot));
}

static void add(JelliPetUi *ui, const JelliGame *game, unsigned choice)
{
    if (ui->quick_count < QUICK_LIMIT && available(ui, game, choice) == JELLI_OK)
        ui->quick_choices[ui->quick_count++] = (uint8_t)choice;
}

static void collect_choices(JelliPetUi *ui, const JelliGame *game, unsigned stat)
{
    ui->quick_count = 0u;
    ui->quick_pet_id = game->pets[game->active].id;
    if (stat == 1u)
        add(ui, game, QUICK_ACTION + JELLI_UI_ACTION_FEED);
    else if (stat == 2u || stat == 7u)
        add(ui, game, QUICK_ACTION + JELLI_UI_ACTION_REST_WAKE);
    else if (stat == 3u)
        add(ui, game, QUICK_ACTION + JELLI_UI_ACTION_WASH);
    else if (stat == 8u)
        add(ui, game, QUICK_ACTION + JELLI_UI_ACTION_WATER);
    unsigned meter = stat == 8u                 ? JELLI_ACTIVITY_HYDRATION
                     : stat == 6u               ? JELLI_ACTIVITY_BOND
                     : stat >= 1u && stat <= 5u ? stat - 1u
                                                : JELLI_ACTIVITY_METERS;
    for (unsigned id = 0u; id < jelli_moment_count && ui->quick_count < QUICK_LIMIT; ++id) {
        const JelliMoment *moment = &jelli_moments[id];
        if ((meter < JELLI_ACTIVITY_METERS && moment->gains[meter]) ||
            (stat == 0u && (moment->gains[3] || moment->gains[4] || moment->gains[6])))
            add(ui, game, id);
    }
}

bool jelli_pet_quick_open(JelliPetUi *ui, const JelliGame *game, int x, int y)
{
    if (ui->menu_open || x < 90 || x >= 376 || y < 282 || y >= 382)
        return false;
    collect_choices(ui, game, ui->last_view.stat_index);
    ui->quick_menu = true;
    ui->activity_count = ui->quick_count;
    ui->activity_page = 0u;
    ui->activity_snapshot = true;
    ui->discovery_pet = ui->quick_pet_id;
    ui->menu_open = true;
    ui->page = JELLI_UI_MOMENTS;
    ui->result = JELLI_OK;
    return true;
}

bool jelli_pet_quick_button(const JelliPetUi *ui, unsigned slot, bool asleep,
                            JelliPetUiButton *button)
{
    unsigned choice = jelli_pet_quick_choice(ui, slot);
    if (choice == UINT8_MAX)
        return false;
    if (choice >= QUICK_ACTION) {
        unsigned action = choice - QUICK_ACTION;
        unsigned page = action == JELLI_UI_ACTION_WASH        ? JELLI_UI_HEALTH
                        : action == JELLI_UI_ACTION_REST_WAKE ? JELLI_UI_SETTINGS
                                                              : JELLI_UI_HOME;
        unsigned source = action == JELLI_UI_ACTION_WASH        ? 4u
                          : action == JELLI_UI_ACTION_REST_WAKE ? 3u
                          : action == JELLI_UI_ACTION_WATER     ? 3u
                                                                : 2u;
        if (!jelli_pet_ui_button((JelliPetPage)page, source, asleep, true, button))
            return false;
    } else if (!jelli_pet_activity_button(choice / 5u, choice % 5u + 1u, asleep, button))
        return false;
    static const JelliRect positions[4] = {
        {62u, 63u, 96u, 96u}, {308u, 63u, 96u, 96u}, {357u, 185u, 96u, 96u}, {13u, 185u, 96u, 96u}};
    button->bounds = positions[slot - 1u];
    return true;
}

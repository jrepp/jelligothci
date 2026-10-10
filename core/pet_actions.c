#include "jelli/pet_ui.h"
#include "pet_gallery.h"
#include "pet_collection.h"
#include "pet_food.h"

_Static_assert(sizeof(JelliGame) <= 4096u, "Action preflight workspace exceeds budget");

bool jelli_pet_ui_command(const JelliPetUi *ui, const JelliGame *game, JelliPetUiAction action,
                          JelliCommand *command)
{
    static const int8_t kinds[] = {-1,
                                   -1,
                                   JELLI_CMD_REST,
                                   -1,
                                   -1,
                                   -1,
                                   JELLI_CMD_CARE,
                                   JELLI_CMD_PLAY,
                                   JELLI_CMD_CLEAN,
                                   -1,
                                   JELLI_CMD_GIFT,
                                   JELLI_CMD_CLAIM,
                                   JELLI_CMD_TRAVEL,
                                   -1,
                                   JELLI_CMD_BEDTIME,
                                   -1,
                                   -1,
                                   JELLI_CMD_MOMENT,
                                   JELLI_CMD_MOMENT,
                                   JELLI_CMD_MOMENT,
                                   JELLI_CMD_MOMENT,
                                   JELLI_CMD_MOMENT,
                                   -1,
                                   JELLI_CMD_HEALTH,
                                   JELLI_CMD_HEALTH,
                                   JELLI_CMD_HEALTH,
                                   JELLI_CMD_HEALTH,
                                   JELLI_CMD_HEALTH,
                                   JELLI_CMD_WATER,
                                   JELLI_CMD_EXERCISE};
    if ((unsigned)action >= sizeof(kinds) / sizeof(kinds[0]) || kinds[action] < 0)
        return false;
    const JelliPet *pet = &game->pets[game->active];
    *command = (JelliCommand){(JelliCommandKind)kinds[action], pet->id, 0u};
    if (command->kind == JELLI_CMD_REST)
        command->kind = pet->asleep ? JELLI_CMD_WAKE : JELLI_CMD_REST;

    else if (command->kind == JELLI_CMD_TRAVEL)
        command->value = pet->location ? 0u : 1u;
    else if (command->kind == JELLI_CMD_BEDTIME)
        command->value = (pet->bedtime + 1u) % 24u;
    else if (command->kind == JELLI_CMD_MOMENT)
        command->value = action == JELLI_UI_ACTION_SUGGEST
                             ? jelli_pet_suggested_moment(pet, ui)
                             : (unsigned)action - JELLI_UI_ACTION_BREAKFAST;
    else if (command->kind == JELLI_CMD_HEALTH)
        command->value = (ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH)
                             ? jelli_pet_health_action(ui)
                             : (unsigned)action - JELLI_UI_ACTION_BRUSH;
    return true;
}

static JelliResult menu_available(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (!slot || slot > 6u)
        return JELLI_OK;
    if (ui->page == JELLI_UI_SETTINGS && (ui->clock_edit || slot == 4u)) {
        if (ui->clock_edit && ((slot == 1u && ui->timezone_minutes <= -720) ||
                               (slot == 2u && ui->timezone_minutes >= 840)))
            return JELLI_NOT_READY;
        return JELLI_OK;
    }
    if ((ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH) && ui->clicker_done)
        return JELLI_NOT_READY;
    JelliPetUiAction action =
        (ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH)
            ? JELLI_UI_ACTION_BRUSH
            : jelli_pet_ui_item(ui->page, slot - 1u, game->pets[game->active].asleep).action;
    JelliCommand command;
    return jelli_pet_ui_command(ui, game, action, &command)
               ? jelli_game_check(game, command, &ui->action_scratch)
               : JELLI_OK;
}

JelliResult jelli_pet_ui_available(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (slot && ui->page == JELLI_UI_FOOD)
        return jelli_pet_food_available(ui, game, slot);
    if (slot && ui->page >= JELLI_UI_PETS && ui->page <= JELLI_UI_EVOLUTIONS)
        return jelli_pet_collection_available(ui, game, slot);
    if (slot && (ui->page == JELLI_UI_COLLECTION || ui->page == JELLI_UI_PRESENT_ACTION ||
                 (!ui->menu_open && (ui->catch_seen || ui->latched_prize))))
        return jelli_pet_gallery_available(ui, game, slot);
    return menu_available(ui, game, slot);
}

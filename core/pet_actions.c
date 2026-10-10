#include "jelli/locations.h"
#include "jelli/pet_ui.h"
#include "jelli/activities.h"
#include "pet_gallery.h"
#include "pet_collection.h"
#include "pet_food.h"

_Static_assert(sizeof(JelliGame) <= 4096u, "Action preflight workspace exceeds budget");

bool jelli_pet_moment_for_action(JelliPetUiAction action, unsigned *moment)
{
    /* Ring actions name moments by ID in content/activities.json. */
    static const struct {
        JelliPetUiAction action;
        uint8_t moment;
    } moments[] = {{JELLI_UI_ACTION_BREAKFAST, 0u}, {JELLI_UI_ACTION_TEA, 1u},
                   {JELLI_UI_ACTION_OUTING, 2u},    {JELLI_UI_ACTION_MOVIE, 3u},
                   {JELLI_UI_ACTION_READING, 4u},   {JELLI_UI_ACTION_STRETCH, 8u}};
    for (unsigned i = 0u; i < sizeof(moments) / sizeof(moments[0]); ++i)
        if (moments[i].action == action && moments[i].moment < jelli_moment_count) {
            *moment = moments[i].moment;
            return true;
        }
    return false;
}

static unsigned activity_value(const JelliPetUi *ui, const JelliPet *pet, JelliPetUiAction action,
                               JelliCommandKind kind)
{
    unsigned value = 0u;
    if (kind == JELLI_CMD_MOMENT && action == JELLI_UI_ACTION_SUGGEST)
        return jelli_pet_suggested_moment(pet, ui);
    if (kind == JELLI_CMD_MOMENT)
        (void)jelli_pet_moment_for_action(action, &value);
    else if (jelli_pet_page_is_routine(ui->page))
        value = jelli_pet_health_action(ui);
    else
        (void)jelli_pet_routine_for_action(action, NULL, &value);
    return value;
}

bool jelli_pet_ui_command(const JelliPetUi *ui, const JelliGame *game, JelliPetUiAction action,
                          JelliCommand *command)
{
    /* Command per action; -1 for actions that navigate or are handled by their page. */
    static const int8_t kinds[JELLI_UI_ACTION_COUNT] = {
        [JELLI_UI_ACTION_FEED] = -1,
        [JELLI_UI_ACTION_CARE] = -1,
        [JELLI_UI_ACTION_REST_WAKE] = JELLI_CMD_REST,
        [JELLI_UI_ACTION_COLLECTION] = -1,
        [JELLI_UI_ACTION_MORE] = -1,
        [JELLI_UI_ACTION_SETTINGS] = -1,
        [JELLI_UI_ACTION_BASIC_CARE] = JELLI_CMD_CARE,
        [JELLI_UI_ACTION_PLAY] = JELLI_CMD_PLAY,
        [JELLI_UI_ACTION_CLEAN_WAKE] = JELLI_CMD_CLEAN,
        [JELLI_UI_ACTION_HOME] = -1,
        [JELLI_UI_ACTION_GIFT] = JELLI_CMD_GIFT,
        [JELLI_UI_ACTION_CLAIM] = JELLI_CMD_CLAIM,
        [JELLI_UI_ACTION_TRAVEL] = JELLI_CMD_TRAVEL,
        [JELLI_UI_ACTION_SWITCH_PET] = -1,
        [JELLI_UI_ACTION_BEDTIME] = JELLI_CMD_BEDTIME,
        [JELLI_UI_ACTION_SAVE] = -1,
        [JELLI_UI_ACTION_MOMENTS] = -1,
        [JELLI_UI_ACTION_BREAKFAST] = JELLI_CMD_MOMENT,
        [JELLI_UI_ACTION_TEA] = JELLI_CMD_MOMENT,
        [JELLI_UI_ACTION_OUTING] = JELLI_CMD_MOMENT,
        [JELLI_UI_ACTION_MOVIE] = JELLI_CMD_MOMENT,
        [JELLI_UI_ACTION_SUGGEST] = JELLI_CMD_MOMENT,
        [JELLI_UI_ACTION_HEALTH] = -1,
        [JELLI_UI_ACTION_BRUSH] = JELLI_CMD_HEALTH,
        [JELLI_UI_ACTION_MEDICINE] = JELLI_CMD_HEALTH,
        [JELLI_UI_ACTION_SHOT] = JELLI_CMD_HEALTH,
        [JELLI_UI_ACTION_WASH] = JELLI_CMD_HEALTH,
        [JELLI_UI_ACTION_STRETCH] = JELLI_CMD_MOMENT,
        [JELLI_UI_ACTION_WATER] = JELLI_CMD_WATER,
        [JELLI_UI_ACTION_EXERCISE] = JELLI_CMD_EXERCISE,
        [JELLI_UI_ACTION_VOLUME_DOWN] = JELLI_CMD_VOLUME,
        [JELLI_UI_ACTION_VOLUME_UP] = JELLI_CMD_VOLUME,
        [JELLI_UI_ACTION_READING] = JELLI_CMD_MOMENT,
        [JELLI_UI_ACTION_POTTY] = JELLI_CMD_HEALTH,
    };
    if ((unsigned)action >= sizeof(kinds) / sizeof(kinds[0]) || kinds[action] < 0)
        return false;
    const JelliPet *pet = &game->pets[game->active];
    *command = (JelliCommand){(JelliCommandKind)kinds[action], pet->id, 0u};
    if (command->kind == JELLI_CMD_VOLUME)
        command->value = action == JELLI_UI_ACTION_VOLUME_UP
                             ? (game->volume >= 90u ? 100u : game->volume + 10u)
                             : (game->volume <= 10u ? 0u : game->volume - 10u);
    else if (command->kind == JELLI_CMD_REST)
        command->kind = pet->asleep ? JELLI_CMD_WAKE : JELLI_CMD_REST;

    else if (command->kind == JELLI_CMD_TRAVEL)
        command->value = (pet->location + 1u) % jelli_location_count;
    else if (command->kind == JELLI_CMD_BEDTIME)
        command->value = (pet->bedtime + 1u) % 24u;
    else if (command->kind == JELLI_CMD_MOMENT || command->kind == JELLI_CMD_HEALTH)
        command->value = activity_value(ui, pet, action, command->kind);
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
    if (jelli_pet_page_is_routine(ui->page) && ui->clicker_done)
        return JELLI_NOT_READY;
    if (ui->page == JELLI_UI_MOMENTS) {
        if (slot == 6u)
            return JELLI_OK;
        unsigned choice = jelli_pet_activity_choice(ui, slot);
        JelliCommand command = {
            choice == jelli_moment_count ? JELLI_CMD_EXERCISE : JELLI_CMD_MOMENT,
            game->pets[game->active].id, choice == jelli_moment_count ? 0u : choice};
        return jelli_game_check(game, command, &ui->action_scratch);
    }
    JelliPetUiAction action =
        jelli_pet_page_is_routine(ui->page)
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

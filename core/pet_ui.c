#include "jelli/pet_ui.h"
#include <stddef.h>

typedef struct {
    JelliPetUiAction action;
    const char *label;
    const char *sleep_label;
} UiAction;

static const UiAction pages[5][6] = {{{JELLI_UI_ACTION_FEED, "FEED", NULL},
                                      {JELLI_UI_ACTION_CARE, "CARE", NULL},
                                      {JELLI_UI_ACTION_REST_WAKE, "REST", "WAKE"},
                                      {JELLI_UI_ACTION_COLLECTION, "COLLECT", NULL},
                                      {JELLI_UI_ACTION_MORE, "MORE", NULL},
                                      {JELLI_UI_ACTION_SETTINGS, "SETTINGS", NULL}},
                                     {{JELLI_UI_ACTION_BASIC_CARE, "BASIC CARE", NULL},
                                      {JELLI_UI_ACTION_PLAY, "PLAY", NULL},
                                      {JELLI_UI_ACTION_CLEAN_WAKE, "CLEAN", "WAKE"},
                                      {JELLI_UI_ACTION_REST_WAKE, "REST", "WAKE"},
                                      {JELLI_UI_ACTION_MORE, "MORE", NULL},
                                      {JELLI_UI_ACTION_HOME, "HOME", NULL}},
                                     {{JELLI_UI_ACTION_GIFT, "GIFT", NULL},
                                      {JELLI_UI_ACTION_CLAIM, "CLAIM", NULL},
                                      {JELLI_UI_ACTION_TRAVEL, "TRAVEL", NULL},
                                      {JELLI_UI_ACTION_SWITCH_PET, "SWITCH PET", NULL},
                                      {JELLI_UI_ACTION_BEDTIME, "BEDTIME +1H", NULL},
                                      {JELLI_UI_ACTION_HOME, "HOME", NULL}},
                                     {{JELLI_UI_ACTION_HOME, "HOME", NULL},
                                      {JELLI_UI_ACTION_SWITCH_PET, "SWITCH PET", NULL},
                                      {JELLI_UI_ACTION_MORE, "MORE", NULL},
                                      {JELLI_UI_ACTION_CARE, "CARE", NULL},
                                      {JELLI_UI_ACTION_SETTINGS, "SETTINGS", NULL},
                                      {JELLI_UI_ACTION_SAVE, "SAVE", NULL}},
                                     {{JELLI_UI_ACTION_BEDTIME, "BEDTIME +1H", NULL},
                                      {JELLI_UI_ACTION_SAVE, "SAVE", NULL},
                                      {JELLI_UI_ACTION_HOME, "HOME", NULL},
                                      {JELLI_UI_ACTION_CARE, "CARE", NULL},
                                      {JELLI_UI_ACTION_MORE, "MORE", NULL},
                                      {JELLI_UI_ACTION_COLLECTION, "COLLECT", NULL}}};

void jelli_pet_ui_init(JelliPetUi *ui)
{
    if (ui == NULL)
        return;
    *ui = (JelliPetUi){.page = JELLI_UI_HOME, .result = JELLI_OK};
}

JelliPetUiItem jelli_pet_ui_item(JelliPetPage page, unsigned item, bool asleep)
{
    if ((unsigned)page >= 5u || item >= 6u)
        return (JelliPetUiItem){"", JELLI_UI_ACTION_HOME};
    UiAction value = pages[page][item];
    const char *label = asleep && value.sleep_label != NULL ? value.sleep_label : value.label;
    return (JelliPetUiItem){label, value.action};
}

static JelliResult run_command(JelliGame *game, JelliCommandKind kind, uint32_t value)
{
    JelliCommand command = {kind, game->pets[game->active].id, value};
    return jelli_game_command(game, command);
}

static bool navigate(JelliPetUi *ui, JelliPetUiAction action)
{
    if (action == JELLI_UI_ACTION_CARE)
        ui->page = JELLI_UI_CARE;
    else if (action == JELLI_UI_ACTION_COLLECTION)
        ui->page = JELLI_UI_COLLECTION;
    else if (action == JELLI_UI_ACTION_MORE)
        ui->page = JELLI_UI_MORE;
    else if (action == JELLI_UI_ACTION_SETTINGS)
        ui->page = JELLI_UI_SETTINGS;
    else if (action == JELLI_UI_ACTION_HOME)
        ui->page = JELLI_UI_HOME;
    else
        return false;
    return true;
}

static JelliResult dispatch_action(JelliGame *game, JelliPetUiAction action)
{
    static const JelliCommandKind command_kinds[] = {
        JELLI_CMD_FEED,   JELLI_CMD_FEED,     JELLI_CMD_REST,    JELLI_CMD_FEED,
        JELLI_CMD_FEED,   JELLI_CMD_FEED,     JELLI_CMD_CARE,    JELLI_CMD_PLAY,
        JELLI_CMD_CLEAN,  JELLI_CMD_FEED,     JELLI_CMD_GIFT,    JELLI_CMD_CLAIM,
        JELLI_CMD_TRAVEL, JELLI_CMD_ACTIVATE, JELLI_CMD_BEDTIME, JELLI_CMD_FEED};
    if ((unsigned)action >= sizeof(command_kinds) / sizeof(command_kinds[0]))
        return JELLI_INVALID_TARGET;
    JelliPet *pet = &game->pets[game->active];
    switch (command_kinds[action]) {
    case JELLI_CMD_FEED:
        return run_command(game, JELLI_CMD_FEED, 0u);
    case JELLI_CMD_PLAY:
        return run_command(game, JELLI_CMD_PLAY, 0u);
    case JELLI_CMD_CLEAN:
        return run_command(game, pet->asleep ? JELLI_CMD_WAKE : JELLI_CMD_CLEAN, 0u);
    case JELLI_CMD_CARE:
        return run_command(game, JELLI_CMD_CARE, 0u);
    case JELLI_CMD_REST:
        return run_command(game, pet->asleep ? JELLI_CMD_WAKE : JELLI_CMD_REST, 0u);
    case JELLI_CMD_WAKE:
        return run_command(game, JELLI_CMD_WAKE, 0u);
    case JELLI_CMD_GIFT:
        return run_command(game, JELLI_CMD_GIFT, 0u);
    case JELLI_CMD_CLAIM:
        return run_command(game, JELLI_CMD_CLAIM, 0u);
    case JELLI_CMD_ACTIVATE: {
        uint8_t target = game->active == 0u ? 1u : 0u;
        return run_command(game, JELLI_CMD_ACTIVATE, game->pets[target].id);
    }
    case JELLI_CMD_TRAVEL:
        return run_command(game, JELLI_CMD_TRAVEL, pet->location == 0u ? 1u : 0u);
    case JELLI_CMD_BEDTIME:
        return run_command(game, JELLI_CMD_BEDTIME, (pet->bedtime + 1u) % 24u);
    }
    return JELLI_INVALID_TARGET;
}

static bool action_persists(JelliPetUiAction action)
{
    switch (action) {
    case JELLI_UI_ACTION_GIFT:
    case JELLI_UI_ACTION_CLAIM:
    case JELLI_UI_ACTION_TRAVEL:
    case JELLI_UI_ACTION_SWITCH_PET:
    case JELLI_UI_ACTION_BEDTIME:
    case JELLI_UI_ACTION_SAVE:
        return true;
    case JELLI_UI_ACTION_FEED:
    case JELLI_UI_ACTION_CARE:
    case JELLI_UI_ACTION_REST_WAKE:
    case JELLI_UI_ACTION_COLLECTION:
    case JELLI_UI_ACTION_MORE:
    case JELLI_UI_ACTION_SETTINGS:
    case JELLI_UI_ACTION_BASIC_CARE:
    case JELLI_UI_ACTION_PLAY:
    case JELLI_UI_ACTION_CLEAN_WAKE:
    case JELLI_UI_ACTION_HOME:
        return false;
    }
    return false;
}

static void execute(JelliPetUi *ui, JelliGame *game, JelliPetUiAction action)
{
    if (navigate(ui, action)) {
        ui->result = JELLI_OK;
        return;
    }
    JelliResult result = dispatch_action(game, action);
    ui->result = result;
    if (result == JELLI_OK && action_persists(action)) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

void jelli_pet_ui_tap(JelliPetUi *ui, JelliGame *game, int x, int y)
{
    if (ui == NULL || game == NULL || game->resuming || !jelli_game_valid(game) || x < 0 || y < 0 ||
        x >= 466 || y >= 466)
        return;
    unsigned ux = (unsigned)x;
    unsigned uy = (unsigned)y;
    if (ux < 58u || uy < 310u || ux >= 408u || uy >= 408u)
        return;
    unsigned cell_x = ux - 58u;
    unsigned cell_y = uy - 310u;
    int dx = 2 * x - 465;
    int dy = 2 * y - 465;
    if (cell_x % 116u >= 112u || cell_y % 49u >= 44u || dx * dx + dy * dy > 466 * 466)
        return;
    unsigned item = (cell_y / 49u) * 3u + cell_x / 116u;
    JelliPetUiItem selected = jelli_pet_ui_item(ui->page, item, game->pets[game->active].asleep);
    execute(ui, game, selected.action);
}

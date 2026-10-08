#include "jelli/pet_ui.h"
#include "game_internal.h"
#include <stddef.h>

typedef struct {
    JelliPetUiAction action;
    const char *label;
    const char *sleep_label;
} UiAction;

/* Empty slots are neither drawn nor hit-tested. Slot IDs remain stable for debug clients. */
static const UiAction pages[7][6] = {{{JELLI_UI_ACTION_CARE, "CARE", NULL},
                                      {JELLI_UI_ACTION_MOMENTS, "MOMENTS", NULL},
                                      {0},
                                      {0},
                                      {JELLI_UI_ACTION_MORE, "GIFTS", NULL},
                                      {JELLI_UI_ACTION_SETTINGS, "SETTINGS", NULL}},
                                     {{JELLI_UI_ACTION_FEED, "FEED", NULL},
                                      {JELLI_UI_ACTION_BASIC_CARE, "BASIC CARE", NULL},
                                      {JELLI_UI_ACTION_CLEAN_WAKE, "CLEAN", NULL},
                                      {JELLI_UI_ACTION_PLAY, "PLAY", NULL},
                                      {JELLI_UI_ACTION_HEALTH, "HEALTH", NULL}},
                                     {{JELLI_UI_ACTION_GIFT, "GIFT", NULL},
                                      {JELLI_UI_ACTION_CLAIM, "CLAIM", NULL},
                                      {JELLI_UI_ACTION_TRAVEL, "TRAVEL", NULL}},
                                     {{0}},
                                     {{JELLI_UI_ACTION_BEDTIME, "BEDTIME +1H", NULL},
                                      {JELLI_UI_ACTION_SWITCH_PET, "SWITCH PET", NULL},
                                      {JELLI_UI_ACTION_REST_WAKE, "REST", "WAKE"}},
                                     {{JELLI_UI_ACTION_BREAKFAST, "BREAKFAST", NULL},
                                      {JELLI_UI_ACTION_TEA, "TEA", NULL},
                                      {JELLI_UI_ACTION_OUTING, "GOING OUT", NULL},
                                      {JELLI_UI_ACTION_MOVIE, "MOVIE", NULL}},
                                     {{JELLI_UI_ACTION_BRUSH, "BRUSH TEETH", NULL},
                                      {JELLI_UI_ACTION_MEDICINE, "MEDICINE", NULL},
                                      {JELLI_UI_ACTION_SHOT, "SHOT", NULL},
                                      {JELLI_UI_ACTION_WASH, "WASH", NULL},
                                      {JELLI_UI_ACTION_STRETCH, "STRETCH", NULL}}};

void jelli_pet_ui_init(JelliPetUi *ui)
{
    if (ui == NULL)
        return;
    *ui = (JelliPetUi){.page = JELLI_UI_HOME, .result = JELLI_OK};
}

JelliPetUiItem jelli_pet_ui_item(JelliPetPage page, unsigned item, bool asleep)
{
    if ((unsigned)page >= 7u || item >= 6u)
        return (JelliPetUiItem){"", JELLI_UI_ACTION_HOME};
    UiAction value = pages[page][item];
    const char *label = asleep && value.sleep_label != NULL ? value.sleep_label : value.label;
    return (JelliPetUiItem){label ? label : "", value.action};
}

static JelliResult run_command(JelliGame *game, JelliCommandKind kind, uint32_t value)
{
    JelliCommand command = {kind, game->pets[game->active].id, value};
    return jelli_game_command(game, command);
}

static bool navigate(JelliPetUi *ui, JelliPetUiAction action)
{
    if (action == JELLI_UI_ACTION_MOMENTS)
        ui->page = JELLI_UI_MOMENTS;
    else if (action == JELLI_UI_ACTION_CARE)
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

static JelliResult run_moment(const JelliPetUi *ui, JelliGame *game, JelliPetUiAction action)
{
    unsigned moment = action == JELLI_UI_ACTION_SUGGEST
                          ? jelli_pet_suggested_moment(&game->pets[game->active], ui)
                          : (unsigned)action - JELLI_UI_ACTION_BREAKFAST;
    if (moment >= 4u)
        return JELLI_INVALID_TARGET;
    return run_command(game, JELLI_CMD_MOMENT, moment);
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
        return run_command(game, JELLI_CMD_CLEAN, 0u);
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
    case JELLI_CMD_TOUCH:
    case JELLI_CMD_MOMENT:
    case JELLI_CMD_HEALTH:
        return JELLI_INVALID_TARGET;
    case JELLI_CMD_BEDTIME:
        return run_command(game, JELLI_CMD_BEDTIME, (pet->bedtime + 1u) % 24u);
    }
    return JELLI_INVALID_TARGET;
}

static bool action_persists(JelliPetUiAction action)
{
    static const bool persists[] = {true,  false, true, false, false, false, true, true,
                                    true,  false, true, true,  true,  true,  true, true,
                                    false, true,  true, true,  true,  true};
    return (unsigned)action < sizeof(persists) / sizeof(persists[0]) && persists[action];
}

static bool hit_button(const JelliPetUiButton *button, int x, int y)
{
    int dx = x - (int)(button->bounds.x + button->bounds.width / 2u);
    int dy = y - (int)(button->bounds.y + button->bounds.height / 2u);
    if (button->circular)
        return dx * dx + dy * dy <= 48 * 48;
    return dx >= -72 && dx < 72 && dy >= -32 && dy < 32;
}

static void execute(JelliPetUi *ui, JelliGame *game, JelliPetUiAction action)
{
    if (jelli_pet_health_select(ui, game, action) || navigate(ui, action)) {
        ui->result = JELLI_OK;
        return;
    }
    JelliResult result = JELLI_OK;
    if (action >= JELLI_UI_ACTION_BREAKFAST && action <= JELLI_UI_ACTION_SUGGEST)
        result = run_moment(ui, game, action);
    else if (action != JELLI_UI_ACTION_SAVE)
        result = dispatch_action(game, action);
    ui->result = result;
    if (result == JELLI_OK && action_persists(action)) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

static unsigned input_code(const JelliPetUi *ui, unsigned slot, bool asleep)
{
    if (!slot)
        return !ui->menu_open ? 28u : ui->page == JELLI_UI_HOME ? 29u : 30u;
    if (ui->page >= JELLI_UI_BRUSH)
        return JELLI_UI_ACTION_BRUSH + (unsigned)ui->page - JELLI_UI_BRUSH;
    return (unsigned)jelli_pet_ui_item(ui->page, slot - 1u, asleep).action;
}

static void finish_event(const JelliPetUi *ui, JelliGame *game, uint32_t sequence, unsigned code,
                         JelliEventSnapshot before)
{
    if (game->events && game->events->sequence == sequence)
        jelli_game_emit(game, JELLI_EVENT_INPUT, code, ui->result, (uint32_t)ui->page,
                        &game->pets[game->active], before);
    if (game->events)
        game->events->input = 0u;
}

static void celebrate(JelliPetUi *ui, const JelliPet *pet, int x, int y)
{
    unsigned amount = jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_BURST_COUNT);
    if (ui->page >= JELLI_UI_BRUSH && ui->clicker_done && ui->result == JELLI_OK && amount)
        amount = JELLI_PARTICLE_CAPACITY;
    unsigned spread = jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_SPREAD);
    jelli_particles_burst_tuned(&ui->particles, x, y, ui->result == JELLI_OK, amount, spread);
}

void jelli_pet_ui_tap(JelliPetUi *ui, JelliGame *game, int x, int y)
{
    if (ui == NULL || game == NULL || game->resuming || !jelli_game_valid(game) || x < 0 || y < 0 ||
        x >= 466 || y >= 466)
        return;
    game->clock_known = ui->clock_known;
    game->clock_minute = ui->clock_minute;
    if (jelli_pet_touch_actor(ui, game, x, y))
        return;
    for (unsigned slot = 0u; slot <= 6u; ++slot) {
        if (slot && ui->last_view.ring_moving)
            continue;
        JelliPetUiButton button;
        if (!jelli_pet_ui_control(ui, slot, game->pets[game->active].asleep, &button))
            continue;
        if (!hit_button(&button, x, y))
            continue;
        unsigned code = input_code(ui, slot, game->pets[game->active].asleep);
        JelliEventSnapshot before = jelli_game_observe(game, &game->pets[game->active]);
        uint32_t sequence = game->events ? game->events->sequence : 0u;
        if (game->events)
            game->events->input = (uint8_t)(code + 1u);
        if (slot == 0u) {
            jelli_pet_ui_back(ui);
            ui->result = JELLI_OK;
        } else if (ui->page >= JELLI_UI_BRUSH) {
            jelli_pet_health_tap(ui, game);
        } else {
            JelliPetUiItem selected =
                jelli_pet_ui_item(ui->page, slot - 1u, game->pets[game->active].asleep);
            execute(ui, game, selected.action);
        }
        finish_event(ui, game, sequence, code, before);
        if (ui->result == JELLI_OK)
            ui->sound_pending = true;
        celebrate(ui, &game->pets[game->active], x, y);
        return;
    }
}

bool jelli_pet_ui_take_sound(JelliPetUi *ui, uint64_t now_ms)
{
    if (!ui || !ui->sound_pending)
        return false;
    ui->sound_pending = false;
    if (ui->sound_played && now_ms >= ui->last_sound_ms && now_ms - ui->last_sound_ms < 120u)
        return false;
    ui->sound_played = true;
    ui->last_sound_ms = now_ms;
    return true;
}

unsigned jelli_pet_ui_sound(JelliPetUi *ui, const JelliPet *pet, uint64_t now_ms)
{
    if (!ui || !pet)
        return 0u;
    bool input = ui->sound_pending;
    bool tap = jelli_pet_ui_take_sound(ui, now_ms);
    bool enabled =
        jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_COO_ENABLED) != 0u;
    if (input || !enabled || ui->menu_open || pet->asleep || pet->activity != JELLI_IDLE ||
        ui->coo_pet != pet->id || now_ms < ui->coo_anchor_ms) {
        ui->coo_anchor_ms = now_ms;
        ui->coo_pet = pet->id;
        return tap ? 6u : 0u;
    }
    unsigned interval = jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_COO_MS);
    if (now_ms - ui->coo_anchor_ms < interval)
        return 0u;
    ui->coo_anchor_ms = now_ms; /* One cue after stalls; never catch up in bursts. */
    return 7u;
}

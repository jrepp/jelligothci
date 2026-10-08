#include "jelli/pet_ui.h"

bool jelli_pet_ui_control(const JelliPetUi *ui, unsigned slot, bool asleep,
                          JelliPetUiButton *button)
{
    if (!ui || !button)
        return false;
    if (ui->page == JELLI_UI_HEALTH && ((slot == 2u && (ui->last_view.care_blocked & 1u)) ||
                                        (slot == 3u && (ui->last_view.care_blocked & 2u))))
        return false;
    if (ui->page >= JELLI_UI_BRUSH && ui->page < JELLI_UI_PAGE_COUNT && slot == 1u &&
        ui->menu_open) {
        static const char *const labels[] = {"BRUSH TEETH", "MEDICINE", "SHOT",
                                             "WASH",        "STRETCH",  "FLOSS",
                                             "MOUTHWASH",   "SPIT",     "CLEAN UP"};
        unsigned activity = jelli_pet_health_action(ui);
        unsigned top = ui->actor_bounds.y > 196u ? ui->actor_bounds.y - 104u : 92u;
        *button = (JelliPetUiButton){.bounds = {185u, top, 96u, 96u},
                                     .label = labels[activity],
                                     .icon = 8001u + activity,
                                     .scale = 3u,
                                     .circular = true};
        return true;
    }
    return jelli_pet_ui_button(ui->page, slot, asleep, ui->menu_open, button);
}

unsigned jelli_pet_health_action(const JelliPetUi *ui)
{
    static const unsigned dental[] = {0u, 5u, 0u, 6u, 7u, 8u};
    if (ui->page == JELLI_UI_BRUSH)
        return dental[ui->clicker_stage < 6u ? ui->clicker_stage : 5u];
    return (unsigned)ui->page - JELLI_UI_BRUSH;
}

static uint8_t choose_taps(JelliPetUi *ui, const JelliPet *pet, JelliTunable low, JelliTunable high)
{
    unsigned a = jelli_tunable_get(&ui->tunables, pet->id, pet->form, low);
    unsigned b = jelli_tunable_get(&ui->tunables, pet->id, pet->form, high);
    /* Sort effective endpoints, including mixed global/profile/pet overrides. */
    unsigned minimum = a < b ? a : b, maximum = a > b ? a : b;
    uint32_t bits = ui->routine_random ? ui->routine_random : pet->id ^ UINT32_C(0x64a72839);
    bits ^= bits << 13;
    bits ^= bits >> 17;
    bits ^= bits << 5;
    ui->routine_random = bits;
    return (uint8_t)(minimum + bits % (maximum - minimum + 1u));
}

static void start_round(JelliPetUi *ui, const JelliPet *pet)
{
    ui->clicker_hits = ui->clicker_stage = 0u;
    ui->clicker_done = false;
    ui->clicker_pet = pet->id;
    ui->clicker_goal =
        (uint8_t)jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_ACTIVITY_TAPS);
    if (ui->page == JELLI_UI_MEDICINE)
        ui->clicker_goal = 1u;
    if (ui->page == JELLI_UI_SHOT) {
        ui->clicker_goal = (uint8_t)jelli_pet_shot_goal(pet);
        ui->clicker_hits = pet->shot_until > pet->ticks ? pet->shot_hits : 0u;
    }
    if (ui->page != JELLI_UI_BRUSH)
        return;
    ui->routine_goals[0] = choose_taps(ui, pet, JELLI_TUNE_BRUSH_MIN, JELLI_TUNE_BRUSH_MAX);
    ui->routine_goals[1] = choose_taps(ui, pet, JELLI_TUNE_FLOSS_MIN, JELLI_TUNE_FLOSS_MAX);
    for (unsigned i = 2u; i < 6u; ++i)
        ui->routine_goals[i] =
            (uint8_t)jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_FINISH_TAPS);
    ui->clicker_goal = ui->routine_goals[0];
}

bool jelli_pet_health_select(JelliPetUi *ui, const JelliGame *game, JelliPetUiAction action)
{
    if (action == JELLI_UI_ACTION_HEALTH) {
        ui->page = JELLI_UI_HEALTH;
        return true;
    }
    if (action < JELLI_UI_ACTION_BRUSH || action > JELLI_UI_ACTION_STRETCH)
        return false;
    if (!jelli_pet_health_ready(&game->pets[game->active],
                                (unsigned)action - JELLI_UI_ACTION_BRUSH)) {
        ui->result = JELLI_NOT_READY;
        return true;
    }
    ui->page = (JelliPetPage)(JELLI_UI_BRUSH + (action - JELLI_UI_ACTION_BRUSH));
    start_round(ui, &game->pets[game->active]);
    return true;
}

void jelli_pet_health_tap(JelliPetUi *ui, JelliGame *game)
{
    if (ui->clicker_pet != game->pets[game->active].id) {
        ui->result = JELLI_INVALID_TARGET;
        return;
    }
    if (game->pets[game->active].asleep) {
        ui->result = JELLI_ASLEEP;
        return;
    }
    if (ui->clicker_done) {
        ui->result = JELLI_NOT_READY;
        return;
    }
    ui->result = jelli_game_command(
        game, (JelliCommand){JELLI_CMD_HEALTH, ui->clicker_pet, jelli_pet_health_action(ui)});
    if (ui->result == JELLI_OK) {
        ++ui->clicker_hits;
        if (ui->clicker_hits >= ui->clicker_goal) {
            if (ui->page == JELLI_UI_BRUSH && ui->clicker_stage < 5u) {
                ++ui->clicker_stage;
                ui->clicker_hits = 0u;
                ui->clicker_goal = ui->routine_goals[ui->clicker_stage];
            } else {
                ui->clicker_done = true;
            }
        }
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

void jelli_pet_ui_back(JelliPetUi *ui)
{
    if (ui->menu_open && ui->page >= JELLI_UI_BRUSH)
        ui->page = JELLI_UI_HEALTH;
    else if (ui->menu_open && ui->page == JELLI_UI_HEALTH)
        ui->page = JELLI_UI_CARE;
    else if (ui->menu_open && ui->page != JELLI_UI_HOME)
        ui->page = JELLI_UI_HOME;
    else
        ui->menu_open = !ui->menu_open;
}

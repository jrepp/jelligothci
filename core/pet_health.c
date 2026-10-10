#include "jelli/pet_ui.h"
#include "pet_gallery.h"
#include "pet_collection.h"
#include "pet_food.h"

bool jelli_pet_ui_control(const JelliPetUi *ui, unsigned slot, bool asleep,
                          JelliPetUiButton *button)
{
    if (!ui || !button)
        return false;
    if (ui->menu_open && ui->page == JELLI_UI_FOOD && slot)
        return jelli_pet_food_button(slot, button);
    if (jelli_pet_collection_button(ui, slot, button))
        return true;
    if (jelli_pet_gallery_button(ui, slot, button))
        return true;
    if (ui->menu_open && ui->page == JELLI_UI_SETTINGS && slot && (ui->clock_edit || slot == 4u))
        return jelli_pet_clock_button(ui->clock_edit, slot, button);
    if ((ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH) && slot == 1u &&
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
        JelliCommand next = {JELLI_CMD_HEALTH, ui->clicker_pet, jelli_pet_health_action(ui)};
        bool satisfied = ui->page == JELLI_UI_BRUSH &&
                         jelli_game_check(game, next, &ui->action_scratch) == JELLI_FULL;
        if (satisfied) {
            /* End gently when care has reached its cap; never strand a half-finished routine. */
            ui->clicker_goal = ui->clicker_hits;
            ui->clicker_done = true;
        } else if (ui->clicker_hits >= ui->clicker_goal) {
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
    if (ui->menu_open && ui->page >= JELLI_UI_PETS) {
        if (ui->page == JELLI_UI_FOOD)
            ui->page = JELLI_UI_CARE;
        else if (ui->page == JELLI_UI_PETS)
            ui->page = JELLI_UI_SETTINGS;
        else if (ui->page == JELLI_UI_PET_DETAIL)
            ui->page = JELLI_UI_PETS;
        else if (ui->page == JELLI_UI_EVOLUTIONS)
            ui->page = JELLI_UI_PET_DETAIL;
        else {
            ui->page = ui->present_return;
            ui->menu_open = ui->page != JELLI_UI_HOME;
        }
        return;
    }
    if (ui->menu_open && ui->clock_edit) {
        ui->clock_edit = false;
        return;
    }
    if (ui->menu_open && (ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH))
        ui->page = JELLI_UI_HEALTH;
    else if (ui->menu_open && ui->page == JELLI_UI_HEALTH)
        ui->page = JELLI_UI_CARE;
    else if (ui->menu_open && ui->page != JELLI_UI_HOME)
        ui->page = JELLI_UI_HOME;
    else
        ui->menu_open = !ui->menu_open;
}

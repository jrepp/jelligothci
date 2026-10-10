#include "jelli_asset_ids.h"
#include "jelli/pet_ui.h"
#include "pet_gallery.h"
#include "pet_collection.h"
#include "pet_food.h"

/* One row per routine page, in page order; dental stages share the brush page. */
typedef struct {
    JelliPetUiAction action;
    JelliPetPage page;
    uint8_t activity; /* JelliHealthActivity shown when the routine starts. */
} Routine;
static const Routine routines[] = {
    {JELLI_UI_ACTION_BRUSH, JELLI_UI_BRUSH, JELLI_HEALTH_BRUSH},
    {JELLI_UI_ACTION_MEDICINE, JELLI_UI_MEDICINE, JELLI_HEALTH_MEDICINE},
    {JELLI_UI_ACTION_SHOT, JELLI_UI_SHOT, JELLI_HEALTH_SHOT},
    {JELLI_UI_ACTION_WASH, JELLI_UI_WASH, JELLI_HEALTH_WASH},
    {JELLI_UI_ACTION_STRETCH, JELLI_UI_STRETCH, JELLI_HEALTH_STRETCH},
    {JELLI_UI_ACTION_POTTY, JELLI_UI_POTTY, JELLI_HEALTH_POTTY},
};
#define ROUTINE_COUNT (sizeof(routines) / sizeof(routines[0]))
_Static_assert(JELLI_UI_POTTY - JELLI_UI_BRUSH + 1 == ROUTINE_COUNT,
               "Routine pages are contiguous");

/* Label and icon per JelliHealthActivity. */
static const struct {
    const char *label;
    uint32_t icon;
} health_items[JELLI_HEALTH_COUNT] = {
    [JELLI_HEALTH_BRUSH] = {"BRUSH TEETH", JELLI_ASSET_HEALTH_BRUSH},
    [JELLI_HEALTH_MEDICINE] = {"MEDICINE", JELLI_ASSET_HEALTH_MEDICINE},
    [JELLI_HEALTH_SHOT] = {"SHOT", JELLI_ASSET_HEALTH_SHOT},
    [JELLI_HEALTH_WASH] = {"WASH", JELLI_ASSET_HEALTH_WASH},
    [JELLI_HEALTH_STRETCH] = {"STRETCH", JELLI_ASSET_HEALTH_STRETCH},
    [JELLI_HEALTH_FLOSS] = {"FLOSS", JELLI_ASSET_HEALTH_FLOSS},
    [JELLI_HEALTH_MOUTHWASH] = {"MOUTHWASH", JELLI_ASSET_HEALTH_MOUTHWASH},
    [JELLI_HEALTH_SPIT] = {"SPIT", JELLI_ASSET_HEALTH_SPIT},
    [JELLI_HEALTH_CLEANUP] = {"CLEAN UP", JELLI_ASSET_HEALTH_CLEANUP},
    [JELLI_HEALTH_POTTY] = {"POTTY", JELLI_ASSET_HEALTH_POTTY},
};

bool jelli_pet_page_is_routine(unsigned page)
{
    return page >= JELLI_UI_BRUSH && page <= JELLI_UI_POTTY;
}

bool jelli_pet_routine_for_action(JelliPetUiAction action, JelliPetPage *page, unsigned *activity)
{
    for (unsigned i = 0u; i < ROUTINE_COUNT; ++i) {
        if (routines[i].action != action)
            continue;
        if (page)
            *page = routines[i].page;
        if (activity)
            *activity = routines[i].activity;
        return true;
    }
    return false;
}

JelliPetUiAction jelli_pet_routine_action(unsigned page)
{
    return jelli_pet_page_is_routine(page) ? routines[page - JELLI_UI_BRUSH].action
                                           : JELLI_UI_ACTION_HOME;
}

uint32_t jelli_pet_health_icon(unsigned activity)
{
    return activity < JELLI_HEALTH_COUNT ? health_items[activity].icon : 0u;
}

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
    if (jelli_pet_page_is_routine(ui->page) && slot == 1u && ui->menu_open) {
        unsigned activity = jelli_pet_health_action(ui);
        unsigned top = ui->actor_bounds.y > 196u ? ui->actor_bounds.y - 104u : 92u;
        *button = (JelliPetUiButton){.bounds = {185u, top, 96u, 96u},
                                     .label = health_items[activity].label,
                                     .icon = health_items[activity].icon,
                                     .scale = 3u,
                                     .circular = true};
        return true;
    }
    return jelli_pet_ui_button(ui->page, slot, asleep, ui->menu_open, button);
}

unsigned jelli_pet_health_action(const JelliPetUi *ui)
{
    static const uint8_t dental[] = {JELLI_HEALTH_BRUSH, JELLI_HEALTH_FLOSS,
                                     JELLI_HEALTH_BRUSH, JELLI_HEALTH_MOUTHWASH,
                                     JELLI_HEALTH_SPIT,  JELLI_HEALTH_CLEANUP};
    unsigned last = sizeof(dental) / sizeof(dental[0]) - 1u;
    if (ui->page == JELLI_UI_BRUSH)
        return dental[ui->clicker_stage < last ? ui->clicker_stage : last];
    return jelli_pet_page_is_routine(ui->page) ? routines[ui->page - JELLI_UI_BRUSH].activity
                                               : JELLI_HEALTH_BRUSH;
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
    if (ui->page == JELLI_UI_MEDICINE || ui->page == JELLI_UI_POTTY)
        ui->clicker_goal = 1u; /* Single-effect routines finish on one tap. */
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
    JelliPetPage page;
    unsigned activity;
    if (!jelli_pet_routine_for_action(action, &page, &activity))
        return false;
    if (!jelli_pet_health_ready(&game->pets[game->active], activity)) {
        ui->result = JELLI_NOT_READY;
        return true;
    }
    ui->page = page;
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
    if (ui->menu_open && jelli_pet_page_is_routine(ui->page))
        ui->page = JELLI_UI_HEALTH;
    else if (ui->menu_open && ui->page == JELLI_UI_HEALTH)
        ui->page = JELLI_UI_CARE;
    else if (ui->menu_open && ui->page != JELLI_UI_HOME)
        ui->page = JELLI_UI_HOME;
    else
        ui->menu_open = !ui->menu_open;
}

#include "jelli/pet_ui.h"
#include "game_internal.h"
#include "pet_gallery.h"
#include "pet_collection.h"
#include "pet_food.h"
#include "pet_feedback.h"
#include "jelli/sound.h"
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
                                      {JELLI_UI_ACTION_COLLECTION, "GIFTS", NULL},
                                      {JELLI_UI_ACTION_SETTINGS, "SETTINGS", NULL}},
                                     {{JELLI_UI_ACTION_FEED, "FEED", NULL},
                                      {JELLI_UI_ACTION_BASIC_CARE, "BASIC CARE", NULL},
                                      {JELLI_UI_ACTION_CLEAN_WAKE, "CLEAN", NULL},
                                      {JELLI_UI_ACTION_PLAY, "PLAY", NULL},
                                      {JELLI_UI_ACTION_HEALTH, "HEALTH", NULL},
                                      {JELLI_UI_ACTION_WATER, "WATER", NULL}},
                                     {{JELLI_UI_ACTION_GIFT, "GIFT", NULL},
                                      {JELLI_UI_ACTION_CLAIM, "CLAIM", NULL},
                                      {JELLI_UI_ACTION_TRAVEL, "TRAVEL", NULL}},
                                     {{0}},
                                     {{JELLI_UI_ACTION_BEDTIME, "BEDTIME +1H", NULL},
                                      {JELLI_UI_ACTION_SWITCH_PET, "PETS", NULL},
                                      {JELLI_UI_ACTION_REST_WAKE, "REST", "WAKE"},
                                      {0},
                                      {JELLI_UI_ACTION_VOLUME_DOWN, "VOL -", NULL},
                                      {JELLI_UI_ACTION_VOLUME_UP, "VOL +", NULL}},
                                     {{JELLI_UI_ACTION_BREAKFAST, "BREAKFAST", NULL},
                                      {JELLI_UI_ACTION_TEA, "TEA", NULL},
                                      {JELLI_UI_ACTION_OUTING, "GOING OUT", NULL},
                                      {JELLI_UI_ACTION_MOVIE, "MOVIE", NULL},
                                      {JELLI_UI_ACTION_EXERCISE, "EXERCISE", NULL}},
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

static bool navigate(JelliPetUi *ui, JelliPetUiAction action)
{
    if (action == JELLI_UI_ACTION_FEED)
        ui->page = JELLI_UI_FOOD;
    else if (action == JELLI_UI_ACTION_SWITCH_PET)
        ui->page = JELLI_UI_PETS;
    else if (action == JELLI_UI_ACTION_MOMENTS)
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

static bool action_persists(JelliPetUiAction action)
{
    if (action == JELLI_UI_ACTION_WATER || action == JELLI_UI_ACTION_EXERCISE ||
        action == JELLI_UI_ACTION_VOLUME_DOWN || action == JELLI_UI_ACTION_VOLUME_UP)
        return true;
    static const bool persists[] = {true,  false, true, false, false, false, true, true,
                                    true,  false, true, true,  true,  true,  true, true,
                                    false, true,  true, true,  true,  true};
    return (unsigned)action < sizeof(persists) / sizeof(persists[0]) && persists[action];
}

static bool hit_button(const JelliPetUiButton *button, int x, int y)
{
    int dx = x - (int)(button->bounds.x + button->bounds.width / 2u);
    int dy = y - (int)(button->bounds.y + button->bounds.height / 2u);
    int radius = (int)button->bounds.width / 2;
    if (button->circular)
        return dx * dx + dy * dy <= radius * radius;
    return x >= (int)button->bounds.x && y >= (int)button->bounds.y &&
           x < (int)(button->bounds.x + button->bounds.width) &&
           y < (int)(button->bounds.y + button->bounds.height);
}

static void execute(JelliPetUi *ui, JelliGame *game, JelliPetUiAction action)
{
    if (jelli_pet_health_select(ui, game, action) || navigate(ui, action)) {
        ui->result = JELLI_OK;
        return;
    }
    JelliResult result = JELLI_OK;
    JelliCommand command;
    if (jelli_pet_ui_command(ui, game, action, &command))
        result = jelli_game_command(game, command);
    ui->result = result;
    if (result == JELLI_OK &&
        (game->pets[game->active].activity != JELLI_IDLE || action == JELLI_UI_ACTION_WATER) &&
        action != JELLI_UI_ACTION_SAVE && action != JELLI_UI_ACTION_VOLUME_DOWN &&
        action != JELLI_UI_ACTION_VOLUME_UP) {
        ui->menu_open = false;
        ui->page = JELLI_UI_HOME;
    }
    if (result == JELLI_OK && action_persists(action)) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

static unsigned input_code(const JelliPetUi *ui, unsigned slot, bool asleep)
{
    if (!slot)
        return !ui->menu_open ? 28u : ui->page == JELLI_UI_HOME ? 29u : 30u;
    if (ui->page == JELLI_UI_SETTINGS && (ui->clock_edit || slot == 4u))
        return ui->clock_edit ? 33u + slot : 33u;
    if (ui->page >= JELLI_UI_PETS)
        return 41u + slot;
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

static void activate_slot(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    if (slot == 0u) {
        jelli_pet_ui_back(ui);
        ui->result = JELLI_OK;
    } else if (ui->page == JELLI_UI_FOOD) {
        jelli_pet_food_select(ui, game, slot);
    } else if (ui->page >= JELLI_UI_PETS && ui->page <= JELLI_UI_EVOLUTIONS) {
        jelli_pet_collection_select(ui, game, slot);
    } else if (ui->page == JELLI_UI_PRESENT_ACTION) {
        jelli_pet_gallery_action(ui, game, slot);
    } else if (ui->page == JELLI_UI_COLLECTION) {
        jelli_pet_gallery_select(ui, game, slot);
    } else if (ui->page == JELLI_UI_SETTINGS && (ui->clock_edit || slot == 4u)) {
        int16_t zone = ui->timezone_minutes, adjust = ui->clock_adjust;
        jelli_pet_clock_action(ui, slot);
        if (ui->result == JELLI_OK &&
            (zone != ui->timezone_minutes || adjust != ui->clock_adjust)) {
            game->timezone_minutes = ui->timezone_minutes;
            game->clock_adjust = ui->clock_adjust;
            ui->save_requested = true;
            ui->save_status = JELLI_SAVE_PENDING;
        }
    } else if ((ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH)) {
        jelli_pet_health_tap(ui, game);
    } else {
        JelliPetUiItem selected =
            jelli_pet_ui_item(ui->page, slot - 1u, game->pets[game->active].asleep);
        execute(ui, game, selected.action);
    }
}

static void sync_clock(const JelliPetUi *ui, JelliGame *game)
{
    game->clock_known = ui->clock_known || ui->clock_adjust || ui->timezone_minutes;
    game->clock_minute = jelli_pet_clock_minute(ui, &game->pets[game->active]);
}

void jelli_pet_ui_tap(JelliPetUi *ui, JelliGame *game, int x, int y)
{
    if (ui == NULL || game == NULL || game->resuming || !jelli_game_valid(game) || x < 0 || y < 0 ||
        x >= 466 || y >= 466)
        return;
    sync_clock(ui, game);
    if (jelli_pet_gallery_tap(ui, game, x, y))
        return;
    if (jelli_pet_touch_actor(ui, game, x, y))
        return;
    for (unsigned slot = 0u; slot <= JELLI_PRIZE_COUNT; ++slot) {
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
        JelliResult available = jelli_pet_ui_available(ui, game, slot);
        if (available != JELLI_OK) {
            ui->result = available;
            finish_event(ui, game, sequence, code, before);
            return;
        }
        bool back = !slot && ui->menu_open;
        uint32_t actor_id = game->pets[game->active].id;
        activate_slot(ui, game, slot);
        finish_event(ui, game, sequence, code, before);
        jelli_pet_feedback(ui, game, before, actor_id, back, x, y);
        return;
    }
}

void jelli_pet_ui_swipe(JelliPetUi *ui, JelliGame *game, int dx, int dy)
{
    if (!ui || !game || game->resuming || !jelli_game_valid(game))
        return;
    unsigned code;
    if (dy < 0 && !ui->menu_open) {
        jelli_pet_ui_back(ui);
        code = 28u;
    } else if (dy > 0 && ui->menu_open) {
        code = ui->page == JELLI_UI_HOME ? 29u : 30u;
        jelli_pet_ui_back(ui);
    } else if (dx && !dy && !ui->menu_open && !ui->last_view.ring_moving) {
        jelli_pet_rewards_cancel(&ui->rewards);
        ui->stat_offset = (uint8_t)((ui->stat_offset + (dx < 0 ? 1u : JELLI_PET_STAT_COUNT - 1u)) %
                                    JELLI_PET_STAT_COUNT);
        ui->tile_reset = true;
        code = 31u;
    } else {
        return;
    }
    ui->sound_pending = true;
    ui->sound_cue = (uint8_t)((dy > 0 ? JELLI_SOUND_BACK : JELLI_SOUND_CONFIRM) + 1u);
    ui->result = JELLI_OK;
    const JelliPet *pet = &game->pets[game->active];
    jelli_game_emit(game, JELLI_EVENT_INPUT, code, JELLI_OK, (uint32_t)ui->page, pet,
                    jelli_game_observe(game, pet));
}

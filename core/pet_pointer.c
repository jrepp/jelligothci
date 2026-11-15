#include "jelli/pet_ui.h"
#include "pet_behavior_draw.h"
#include "jelli/sound.h"

#define PET_HOLD_MS 3000u
#define PET_TOUCH_MARGIN 16

static bool inside(JelliRect r, int x, int y, int margin)
{
    return r.width && r.height && x >= (int)r.x - margin && y >= (int)r.y - margin &&
           x < (int)(r.x + r.width) + margin && y < (int)(r.y + r.height) + margin;
}

static bool screen_control(int x, int y)
{
    return (x >= 100 && x < 366 && y >= 50 && y < 72) ||
           (x >= 90 && x < 376 && y >= 282 && y < 382);
}

bool jelli_pet_actor_hit(const JelliPetUi *ui, const JelliGame *game, int x, int y)
{
    if (!ui->menu_open && screen_control(x, y))
        return false;
    if (ui->menu_open || game->resuming || ui->effect ||
        (game->collectible.active && !game->collectible.paused) ||
        jelli_pet_page_is_routine(ui->page) || x < 0 || y < 0 || x >= JELLI_WIDTH ||
        y >= JELLI_HEIGHT)
        return false;
    if (game->prizes.offered && inside((JelliRect){193u, 70u, 80u, 80u}, x, y, 0))
        return false;
    JelliRect mess;
    if (jelli_pet_mess_bounds(ui, ui->last_view.mess, &mess) && inside(mess, x, y, 0))
        return false;
    return inside(ui->actor_bounds, x, y, PET_TOUCH_MARGIN);
}

void jelli_pet_pointer_update(JelliPetUi *ui, const JelliGame *game, uint64_t now)
{
    if (!ui->pointer.active || ui->pointer_consumed)
        return;
    const JelliPet *pet = &game->pets[game->active];
    if (ui->pointer_form != pet->form || ui->pointer_location != pet->location ||
        ui->pointer_activity != pet->activity || game->resuming ||
        ui->pointer_pet != game->pets[game->active].id || ui->pointer_page != ui->page ||
        ui->pointer_menu != ui->menu_open || now < ui->pointer_ms || ui->effect ||
        (game->collectible.active && !game->collectible.paused)) {
        ui->pointer.active = false;
        ui->hold_progress = 0u;
        return;
    }
    if (!ui->pointer_pet_target || ui->pointer_moved)
        return;
    uint64_t elapsed = now - ui->pointer_ms;
    ui->hold_progress = elapsed >= PET_HOLD_MS ? 100u : (uint8_t)(1u + elapsed * 99u / PET_HOLD_MS);
    if (elapsed < PET_HOLD_MS)
        return;
    ui->menu_open = true;
    ui->page = JELLI_UI_SETTINGS;
    ui->settings_return = JELLI_UI_HOME;
    ui->clock_edit = false;
    ui->result = JELLI_OK;
    ui->pointer_consumed = true;
    ui->hold_progress = 0u;
    ui->sound_pending = true;
    ui->sound_cue = JELLI_SOUND_CONFIRM + 1u;
}

void jelli_pet_pointer(JelliPetUi *ui, JelliGame *game, JelliInput input, uint64_t now)
{
    if (input.kind == JELLI_POINTER_CANCEL) {
        ui->pointer.active = false;
        ui->hold_progress = 0u;
        return;
    }
    if (input.kind == JELLI_POINTER_DOWN) {
        jelli_gesture_begin(&ui->pointer, input.x, input.y);
        ui->pointer_ms = now;
        ui->pointer_pet = game->pets[game->active].id;
        ui->pointer_page = (uint8_t)ui->page;
        ui->pointer_form = game->pets[game->active].form;
        ui->pointer_location = game->pets[game->active].location;
        ui->pointer_activity = game->pets[game->active].activity;
        ui->pointer_menu = ui->menu_open;
        ui->pointer_pet_target = jelli_pet_actor_hit(ui, game, input.x, input.y);
        ui->pointer_moved = ui->pointer_consumed = false;
    } else if (ui->pointer.active) {
        /* Range check before subtraction, including out-of-surface cancellation. */
        if (input.x < 0 || input.y < 0 || input.x >= JELLI_WIDTH || input.y >= JELLI_HEIGHT) {
            ui->pointer.active = false;
            ui->hold_progress = 0u;
            return;
        }
        int dx = input.x - ui->pointer.x, dy = input.y - ui->pointer.y;
        if (dx < -18 || dx > 18 || dy < -18 || dy > 18) {
            ui->pointer_moved = true;
            ui->hold_progress = 0u;
        }
    }
    jelli_pet_pointer_update(ui, game, now);
    if (input.kind != JELLI_POINTER_UP)
        return;
    JelliInput action;
    bool released = jelli_gesture_end(&ui->pointer, input.x, input.y, &action);
    ui->hold_progress = 0u;
    if (!released || ui->pointer_consumed)
        return;
    if (action.kind == JELLI_SWIPE)
        jelli_pet_ui_swipe(ui, game, action.x, action.y);
    else if (!ui->pointer_moved) {
        if (ui->pointer_pet_target) {
            ui->menu_open = true; /* Target frozen at press, even if the pet animates. */
            ui->page = JELLI_UI_HOME;
            ui->result = JELLI_OK;
            ui->sound_pending = true;
        } else
            jelli_pet_ui_tap(ui, game, action.x, action.y);
    }
}

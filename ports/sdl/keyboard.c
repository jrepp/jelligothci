#include "keyboard.h"
#include "jelli/collectible.h"

static bool focus_step(JelliPetEngine *pet, bool reverse)
{
    JelliPetUi *ui = &pet->ui;
    unsigned slot = ui->keyboard_focus ? ui->keyboard_slot : 0u;
    for (unsigned n = 0u; n <= JELLI_PRIZE_COUNT; ++n) {
        slot = (slot + (reverse ? JELLI_PRIZE_COUNT : 1u)) % (JELLI_PRIZE_COUNT + 1u);
        JelliPetUiButton button;
        if (jelli_pet_ui_control(ui, slot, pet->game.pets[pet->game.active].asleep, &button)) {
            ui->keyboard_focus = true;
            ui->keyboard_slot = (uint8_t)slot;
            return true;
        }
    }
    return false;
}

static bool activate(JelliPetEngine *pet, unsigned slot, JelliInput *input)
{
    JelliPetUiButton button;
    if (!jelli_pet_ui_control(&pet->ui, slot, pet->game.pets[pet->game.active].asleep, &button))
        return false;
    *input = (JelliInput){.kind = JELLI_TAP,
                          .x = (int)(button.bounds.x + button.bounds.width / 2u),
                          .y = (int)(button.bounds.y + button.bounds.height / 2u)};
    return true;
}

bool jelli_sdl_keyboard(JelliPetEngine *pet, const SDL_Event *event, JelliInput *input)
{
    if (!pet || event->type != SDL_KEYDOWN || event->key.repeat)
        return false;
    SDL_Keycode key = event->key.keysym.sym;
    if (key == SDLK_TAB) {
        (void)focus_step(pet, (event->key.keysym.mod & KMOD_SHIFT) != 0);
        return false; /* Focus is visual only; never synthesize an action. */
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
        return activate(pet, pet->ui.keyboard_focus ? pet->ui.keyboard_slot : 0u, input);
    if (key == SDLK_ESCAPE && pet->ui.menu_open)
        return activate(pet, 0u, input);
    int dx = key == SDLK_LEFT ? -80 : key == SDLK_RIGHT ? 80 : 0;
    int dy = key == SDLK_UP ? -80 : key == SDLK_DOWN ? 80 : 0;
    if (dx || dy) {
        *input = (JelliInput){.kind = JELLI_SWIPE, .x = dx, .y = dy};
        return true;
    }
    return false;
}

void jelli_sdl_focus_lost(JelliPetEngine *pet)
{
    if (!pet || !pet->game.collectible.active || pet->game.collectible.paused)
        return;
    if (jelli_collectible_control(&pet->game, JELLI_COLLECTIBLE_PAUSE) == JELLI_OK) {
        pet->ui.collectible_menu = true;
        pet->ui.save_requested = true;
        pet->ui.save_status = JELLI_SAVE_PENDING;
        jelli_pet_music_sync(pet);
    }
}

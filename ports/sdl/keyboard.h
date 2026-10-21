#ifndef JELLI_SDL_KEYBOARD_H
#define JELLI_SDL_KEYBOARD_H
#include "jelli/pet_engine.h"
#include <SDL.h>
bool jelli_sdl_keyboard(JelliPetEngine *pet, const SDL_Event *event, JelliInput *input);
void jelli_sdl_focus_lost(JelliPetEngine *pet);
#endif

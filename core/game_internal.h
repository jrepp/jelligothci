#ifndef JELLI_GAME_INTERNAL_H
#define JELLI_GAME_INTERNAL_H

#include "jelli/game.h"

void jelli_game_add_clock(JelliGame *game, JelliPet *pet, uint64_t ticks);
void jelli_game_endpoint(JelliGame *game, JelliPet *pet, uint64_t ticks, bool offline);
void jelli_game_apply_effect(JelliGame *game, JelliPet *pet);
bool jelli_game_window(const JelliPet *pet, uint64_t *remaining_ticks);

#endif

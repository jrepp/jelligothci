#ifndef JELLI_GAME_INTERNAL_H
#define JELLI_GAME_INTERNAL_H

#include "jelli/game.h"

void jelli_game_preference(const JelliGame *game, JelliPet *pet);
JelliResult jelli_game_touch(JelliPet *pet);
void jelli_pet_touch_decay(JelliPet *pet, uint64_t ticks);
void jelli_game_endpoint(JelliGame *game, JelliPet *pet, uint64_t ticks, bool offline);
void jelli_game_apply_effect(JelliGame *game, JelliPet *pet);
bool jelli_game_window(const JelliPet *pet, uint64_t *remaining_ticks);

JelliEventSnapshot jelli_game_observe(const JelliGame *game, const JelliPet *pet);
void jelli_game_emit(JelliGame *game, JelliEventKind kind, unsigned code, JelliResult result,
                     uint32_t value, const JelliPet *pet, JelliEventSnapshot before);
JelliResult jelli_game_command_impl(JelliGame *game, JelliCommand command);
#endif

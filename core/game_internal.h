#ifndef JELLI_GAME_INTERNAL_H
#define JELLI_GAME_INTERNAL_H

#include "jelli/game.h"

unsigned jelli_game_preference_gain(const JelliGame *game, const JelliPet *pet, unsigned meter);
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
void jelli_collectible_record_reward(JelliGame *game, unsigned before_ticks);
JelliResult jelli_collectible_command(JelliGame *game, JelliCommand command);
JelliResult jelli_game_set_volume(JelliGame *game, uint32_t value);
#endif

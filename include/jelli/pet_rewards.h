#ifndef JELLI_PET_REWARDS_H
#define JELLI_PET_REWARDS_H

#include "jelli/game.h"
#include "jelli/particles.h"

#define JELLI_PET_STAT_COUNT 8u
#define JELLI_PET_REWARD_CAPACITY 7u

typedef struct {
    uint16_t from, to;
    uint8_t stat;
} JelliPetReward;

typedef struct {
    JelliPetReward items[JELLI_PET_REWARD_CAPACITY];
    int16_t gains[6];
    uint64_t last_ms, elapsed_ms;
    uint32_t cursor, pet_id, command_value, completed;
    uint8_t count, index, activity, command, health_kind;
    bool pending, health, visible, burst;
} JelliPetRewards;

void jelli_pet_rewards_cancel(JelliPetRewards *rewards);
/* Reads only accepted commands and matching effects; ordinary decay is excluded.
 * Returns true once a completed healthy routine may return to the home scene. */
bool jelli_pet_rewards_process(JelliPetRewards *rewards, JelliGame *game, bool health_active,
                               bool health_done, uint32_t health_pet);
/* One bounded, quiet burst per stat; menu visibility suspends the queue. */
bool jelli_pet_rewards_animate(JelliPetRewards *rewards, JelliParticles *particles, uint64_t now_ms,
                               unsigned duration_ms, bool visible, uint8_t *selected,
                               uint16_t *value);
uint16_t jelli_pet_reward_stat(const JelliPet *pet, unsigned stat);

#endif

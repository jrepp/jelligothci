#include "game_internal.h"
#include "jelli/collectible_types.h"

/* Ten deterministic pulses, spaced ~3 seconds apart with bounded jitter. The
 * cumulative amount depends only on permanent play progress, never on restarts. */
static unsigned record_credit(uint32_t pet_id, unsigned prize, unsigned ticks)
{
    unsigned paid = 0u, total = 0u;
    for (unsigned i = 0u; i < 10u; ++i) {
        uint32_t hash = pet_id ^ (prize * UINT32_C(2246822519)) ^ (i * UINT32_C(2654435761));
        hash ^= hash >> 16u;
        unsigned weight = 2u + hash % 3u;
        unsigned due = i == 0u ? 1u : i == 9u ? 300u : i * 33u + 1u + hash % 13u;
        total += weight;
        if (ticks >= due)
            paid += weight;
    }
    return 30u * paid / total;
}

void jelli_collectible_record_reward(JelliGame *game, unsigned before_ticks)
{
    JelliPet *pet = &game->pets[game->active];
    unsigned prize = game->collectible.prize;
    unsigned bit = 1u << (prize - JELLI_COLLECTIBLE_RECORD_PRIZE);
    if (pet->collectible_progress.record_rewarded & bit)
        return;
    unsigned ticks = pet->collectible_progress.play_ticks[prize];
    unsigned gain =
        record_credit(pet->id, prize, ticks) - record_credit(pet->id, prize, before_ticks);
    if (gain) {
        JelliEventSnapshot before = jelli_game_observe(game, pet);
        unsigned value = pet->needs[JELLI_AMUSEMENT] + gain;
        pet->needs[JELLI_AMUSEMENT] = (uint16_t)(value > 1000u ? 1000u : value);
        pet->need_remainders[JELLI_AMUSEMENT] = 0u;
        jelli_game_emit(game, JELLI_EVENT_EFFECT, 0u, JELLI_OK, gain, pet, before);
    }
    if (ticks >= 300u)
        pet->collectible_progress.record_rewarded |= (uint8_t)bit;
}

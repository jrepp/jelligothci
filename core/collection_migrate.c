#include "jelli/collection.h"
#include <string.h>

/* Save versions 1..7 gave the same evolution family two starter records.
 * Retain the active starter's complete state and union both unlock histories.
 * Other families keep their IDs, care state, and order. */
void jelli_collection_merge_starters(JelliGame *game)
{
    int first = jelli_collection_find(game, 1u);
    int second = jelli_collection_find(game, 2u);
    for (unsigned i = 0; i < game->count; ++i)
        game->pets[i].reached_forms |= game->pets[i].form ? 3u : 1u;
    if (second < 0)
        return;
    if (first < 0) {
        game->pets[second].collection_entry = 1u;
        game->new_pets = (uint16_t)((game->new_pets & ~2u) | ((game->new_pets & 2u) >> 1));
        return;
    }
    unsigned keep = game->active == (unsigned)second ? (unsigned)second : (unsigned)first;
    unsigned remove = keep == (unsigned)first ? (unsigned)second : (unsigned)first;
    uint32_t removed_id = game->pets[remove].id;
    JelliPet *pet = &game->pets[keep];
    pet->collection_entry = 1u;
    pet->reached_forms |= game->pets[remove].reached_forms;
    for (unsigned i = 0; i < JELLI_PRIZE_COUNT; ++i)
        if (game->prizes.origin_pet[i] == removed_id)
            game->prizes.origin_pet[i] = pet->id;
    if (game->prizes.offered_pet == removed_id)
        game->prizes.offered_pet = pet->id;
    if (game->sleep_log.pet_id == removed_id)
        game->sleep_log.pet_id = pet->id;
    game->new_pets = (uint16_t)((game->new_pets & ~2u) | ((game->new_pets & 2u) >> 1));
    for (unsigned i = remove; i + 1u < game->count; ++i)
        game->pets[i] = game->pets[i + 1u];
    --game->count;
    memset(&game->pets[game->count], 0, sizeof(game->pets[0]));
    if (game->active > remove)
        --game->active;
}

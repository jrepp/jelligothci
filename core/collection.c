#include "jelli/collection.h"
#include <limits.h>

int jelli_collection_find(const JelliGame *game, unsigned entry)
{
    for (unsigned i = 0u; i < game->count; ++i)
        if (game->pets[i].collection_entry == entry)
            return (int)i;
    return -1;
}

void jelli_collection_migrate(JelliGame *game)
{
    for (unsigned i = 0u; i < game->count; ++i) {
        game->pets[i].collection_entry = (uint8_t)(i + 1u);
        game->pets[i].reached_forms =
            game->pets[i].form <= 1u ? (uint8_t)(1u << game->pets[i].form) : 0u;
    }
}

bool jelli_collection_valid(const JelliGame *game)
{
    uint16_t owned = 0u;
    for (unsigned i = 0u; i < game->count; ++i) {
        const JelliPet *pet = &game->pets[i];
        if (pet->collection_entry < 1u || pet->collection_entry > 9u || pet->reached_forms > 3u)
            return false;
        uint16_t bit = (uint16_t)(1u << (pet->collection_entry - 1u));
        if (owned & bit)
            return false;
        owned |= bit;
    }
    return (game->new_pets & owned) == game->new_pets;
}

static uint32_t unused_id(const JelliGame *game)
{
    /* At most nine occupied IDs: a free ID always exists among 1..10. */
    for (uint32_t id = 1u; id <= 10u; ++id) {
        bool found = false;
        for (unsigned i = 0u; i < game->count; ++i)
            found |= game->pets[i].id == id;
        if (!found)
            return id;
    }
    return 0u;
}

void jelli_collection_unlock(JelliGame *game)
{
    for (unsigned i = 0u; i < 9u && game->count < JELLI_PET_CAPACITY; ++i) {
        const JelliCollectionEntry *entry = &jelli_collection_entries[i];
        if (jelli_collection_find(game, entry->id) >= 0 ||
            (entry->unlock_prize &&
             !(game->prizes.discovered & (1u << (entry->unlock_prize - 1u)))))
            continue;
        uint32_t id = unused_id(game);
        if (!id)
            return;
        game->pets[game->count++] = (JelliPet){.id = id,
                                               .collection_entry = entry->id,
                                               .reached_forms = 1u,
                                               .phase_offset = 324000u,
                                               .bedtime = 22u,
                                               .sleep_duration = 288000u,
                                               .random_state = id,
                                               .needs = {500u, 700u, 700u, 500u, 500u},
                                               .bond = 100u,
                                               .health = JELLI_WELL,
                                               .activity = JELLI_IDLE};
        game->new_pets |= (uint16_t)(1u << i);
        ++game->revision;
    }
}

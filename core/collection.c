#include "jelli/collection.h"
#include <limits.h>
#include <stddef.h>

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
            (uint8_t)(game->pets[i].form <= 1u ? (1u << (game->pets[i].form + 1u)) - 1u : 0u);
    }
}

const JelliEvolutionSet *jelli_collection_set(unsigned entry)
{
    if (entry < 1u || entry > 9u)
        return NULL;
    return &jelli_evolution_sets[jelli_collection_entries[entry - 1u].evolution_set - 1u];
}

unsigned jelli_evolution_mask(const JelliEvolutionSet *set)
{
    unsigned mask = 0u;
    for (unsigned i = 0u; i < set->form_count; ++i)
        mask |= 1u << set->forms[i];
    return mask;
}

unsigned jelli_evolution_form(const JelliEvolutionSet *set, unsigned index)
{
    return index < set->form_count && index < JELLI_SET_FORM_CAPACITY ? set->forms[index]
                                                                      : set->forms[0];
}

int jelli_evolution_index(const JelliEvolutionSet *set, unsigned form)
{
    for (unsigned i = 0u; i < set->form_count; ++i)
        if (set->forms[i] == form)
            return (int)i;
    return -1;
}

bool jelli_collection_growth_due(const JelliPet *pet)
{
    const JelliEvolutionSet *set = jelli_collection_set(pet->collection_entry);
    return set && set->form_count == 2u && pet->form == set->forms[0] &&
           !(pet->reached_forms & (1u << set->forms[1])) && pet->stage_ticks >= set->growth_ticks;
}

void jelli_collection_normalize(JelliGame *game)
{
    /* Runs before validation on decoded saves, so count is not yet trusted. */
    for (unsigned i = 0u; i < game->count && i < JELLI_PET_CAPACITY; ++i) {
        JelliPet *pet = &game->pets[i];
        const JelliEvolutionSet *set = jelli_collection_set(pet->collection_entry);
        /* Only catalog forms are remapped; corrupt IDs stay invalid for validation. */
        unsigned known = (1u << jelli_collection_form_count) - 1u;
        if (!set || pet->form >= jelli_collection_form_count)
            continue;
        if (jelli_evolution_index(set, pet->form) < 0)
            pet->form = set->forms[0];
        unsigned keep = jelli_evolution_mask(set) | (~known & 0xffu);
        pet->reached_forms =
            (uint8_t)((pet->reached_forms & keep) | (1u << set->forms[0]) | (1u << pet->form));
    }
}

bool jelli_collection_valid(const JelliGame *game)
{
    uint16_t owned = 0u;
    for (unsigned i = 0u; i < game->count; ++i) {
        const JelliPet *pet = &game->pets[i];
        const JelliEvolutionSet *set = jelli_collection_set(pet->collection_entry);
        if (!set || jelli_evolution_index(set, pet->form) < 0 ||
            (pet->reached_forms & ~jelli_evolution_mask(set)))
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
        uint8_t form = jelli_collection_set(entry->id)->forms[0];
        game->pets[game->count++] = (JelliPet){.id = id,
                                               .collection_entry = entry->id,
                                               .form = form,
                                               .reached_forms = (uint8_t)(1u << form),
                                               .phase_offset = 324000u,
                                               .bedtime = 22u,
                                               .sleep_duration = 288000u,
                                               .random_state = id,
                                               .needs = {500u, 700u, 700u, 500u, 500u},
                                               .bond = 100u,
                                               .hydration = 700u,
                                               .health = JELLI_WELL,
                                               .activity = JELLI_IDLE};
        game->new_pets |= (uint16_t)(1u << i);
        ++game->revision;
    }
}

JelliResult jelli_collection_set_form(JelliGame *game, uint32_t id, uint32_t index)
{
    for (unsigned i = 0; i < game->count; ++i) {
        JelliPet *pet = &game->pets[i];
        if (pet->id != id)
            continue;
        const JelliEvolutionSet *set = jelli_collection_set(pet->collection_entry);
        if (!set || index >= set->form_count)
            return JELLI_INVALID_TARGET;
        unsigned form = jelli_evolution_form(set, index);
        if (!(pet->reached_forms & (1u << form)))
            return JELLI_NOT_READY;
        if (pet->form == form)
            return JELLI_FULL;
        pet->form = (uint8_t)form;
        ++game->revision;
        return JELLI_OK;
    }
    return JELLI_INVALID_TARGET;
}

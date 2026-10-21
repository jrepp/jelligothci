#include "debug_internal.h"
#include "game_internal.h"
#include "jelli/collection.h"
#include <string.h>

static uint32_t gift_mask(const char *key)
{
    if (!strcmp(key, "all"))
        return JELLI_PRIZE_MASK;
    for (unsigned i = 0u; i < jelli_prize_count; ++i)
        if (!strcmp(key, jelli_prizes[i].key))
            return (uint32_t)(1u << i);
    return 0u;
}

static void edit_gifts(JelliPetEngine *engine, uint32_t mask, bool grant)
{
    JelliGame *game = &engine->game;
    if (grant) {
        for (unsigned i = 0u; i < jelli_prize_count; ++i)
            if ((mask & (1u << i)) && !game->prizes.origin_pet[i])
                game->prizes.origin_pet[i] = game->pets[game->active].id;
        game->prizes.owned |= mask;
        game->prizes.discovered |= mask;
        jelli_collection_unlock(game);
    } else {
        game->prizes.owned &= (uint32_t)~mask;
        if (game->collectible.pet_id && (mask & (1u << game->collectible.prize))) {
            game->collectible = (JelliCollectibleSession){0};
            if (engine->ui.page == JELLI_UI_COLLECTIBLE_PLAY)
                engine->ui.page = JELLI_UI_PRESENT_ACTION;
            jelli_pet_music_sync(engine);
        }
    }
    if (game->prizes.offered && (mask & (1u << (game->prizes.offered - 1u)))) {
        game->prizes.offered = 0u;
        game->prizes.offered_pet = 0u;
    }
    ++game->revision;
    engine->ui.save_requested = true;
    engine->ui.save_status = JELLI_SAVE_PENDING;
    engine->ui.rendered = false;
}

void jelli_debug_gift(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                      unsigned count)
{
    if (count != 5u || (strcmp(words[3], "grant") != 0 && strcmp(words[3], "remove") != 0)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"gift_syntax\"}");
        return;
    }
    uint32_t mask = gift_mask(words[4]);
    if (!mask) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"unknown_gift\"}");
        return;
    }
    JelliGame *game = &engine->game;
    if (debug->captured || game->resuming || !jelli_game_valid(game)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    bool grant = !strcmp(words[3], "grant");
    const JelliPet *pet = &game->pets[game->active];
    JelliEventSnapshot before = jelli_game_observe(game, pet);
    edit_gifts(engine, mask, grant);
    jelli_game_emit(game, JELLI_EVENT_CHEAT, grant ? 9u : 10u, JELLI_OK, mask, pet, before);
    jelli_debug_response(debug, id, "{\"ok\":true,\"gift\":\"updated\"}");
}

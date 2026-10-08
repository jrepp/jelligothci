#include "debug_internal.h"
#include "game_internal.h"
#include <string.h>

static void heal(JelliPet *pet)
{
    for (unsigned i = 0; i < JELLI_NEED_COUNT; ++i) {
        pet->needs[i] = 1000u;
        pet->need_remainders[i] = 0u;
    }
    pet->health = JELLI_WELL;
    pet->activity = JELLI_IDLE;
    pet->interaction_due = 0u;
    pet->touch_load = 0u;
    pet->reaction = pet->reaction_ticks = 0u;
    pet->hunger_low = pet->hunger_counted = false;
    pet->hunger_due = 0u;
}

static bool parse(char **words, unsigned count, unsigned *code, uint32_t *value)
{
    static const char *const names[] = {"fullness", "energy", "clean", "fun", "connection",
                                        "bond",     "food",   "gifts", "heal"};
    if (count < 4u)
        return false;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(words[3], names[i]) != 0)
            continue;
        *code = i;
        if (i == 8u)
            return count == 4u;
        return count == 5u && jelli_debug_number(words[4], value) &&
               *value <= (i >= 6u ? JELLI_STACK_LIMIT : 100u);
    }
    return false;
}

static bool apply(JelliGame *game, unsigned code, uint32_t value)
{
    JelliPet *pet = &game->pets[game->active];
    JelliPet previous = *pet;
    uint16_t food = game->food, gifts = game->gifts;
    if (code < JELLI_NEED_COUNT) {
        pet->needs[code] = (uint16_t)(value * 10u);
        pet->need_remainders[code] = 0u;
    } else if (code == 5u) {
        pet->bond = (uint16_t)(value * 10u);
    } else if (code == 6u) {
        game->food = (uint16_t)value;
    } else if (code == 7u) {
        game->gifts = (uint16_t)value;
    } else {
        heal(pet);
    }
    if (jelli_game_valid(game))
        return true;
    *pet = previous;
    game->food = food;
    game->gifts = gifts;
    return false;
}

void jelli_debug_cheat(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                       unsigned count)
{
    unsigned code = 0;
    uint32_t value = 0;
    if (!parse(words, count, &code, &value)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"cheat_range_or_name\"}");
        return;
    }
    JelliGame *game = &engine->game;
    if (debug->captured || game->resuming || !jelli_game_valid(game)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    const JelliPet *pet = &game->pets[game->active];
    JelliEventSnapshot before = jelli_game_observe(game, pet);
    if (!apply(game, code, value)) {
        jelli_debug_response(
            debug, id,
            "{\"ok\":false,\"error\":\"conflicts_with_active_recovery_or_item_reservation\"}");
        return;
    }
    ++game->revision;
    engine->ui.save_requested = true;
    engine->ui.save_status = JELLI_SAVE_PENDING;
    engine->ui.result = JELLI_OK;
    jelli_game_emit(game, JELLI_EVENT_CHEAT, code, JELLI_OK, value, pet, before);
    jelli_debug_response(debug, id, "{\"ok\":true,\"cheat\":\"applied\"}");
}

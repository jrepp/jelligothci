#include "jelli/behavior.h"
#include "game_internal.h"
#include <stddef.h>

static const JelliPet *command_actor(const JelliGame *game, JelliCommand command)
{
    if (command.kind == JELLI_CMD_FORM)
        for (unsigned i = 0; i < game->count; ++i)
            if (game->pets[i].id == command.actor_id)
                return &game->pets[i];
    return &game->pets[game->active];
}

JelliResult jelli_game_command(JelliGame *game, JelliCommand command)
{
    if (!jelli_game_valid(game))
        return JELLI_INVALID_TARGET;
    JelliEventSnapshot before = jelli_game_observe(game, command_actor(game, command));
    const JelliPet *active = &game->pets[game->active];
    unsigned location = active->location, activity = (unsigned)active->activity;
    JelliResult result = jelli_game_command_impl(game, command);
    if (result == JELLI_OK) {
        jelli_behavior_command(game, command, location, activity);
    }
    jelli_game_emit(game, JELLI_EVENT_COMMAND, (unsigned)command.kind, result, command.value,
                    command_actor(game, command), before);
    return result;
}

JelliResult jelli_game_check(const JelliGame *game, JelliCommand command, JelliGame *scratch)
{
    if (!game || !scratch || game == scratch)
        return JELLI_INVALID_TARGET;
    *scratch = *game;
    scratch->events = NULL;
    return jelli_game_command_impl(scratch, command);
}

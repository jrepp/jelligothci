#include "session.h"

void jelli_sdl_demo(JelliPetEngine *engine, unsigned long frame)
{
    static const struct {
        unsigned long frame;
        JelliCommandKind kind;
        uint32_t value;
    } steps[] = {
        {1u, JELLI_CMD_FEED, 0u},       {60u, JELLI_CMD_CLAIM, 0u}, {70u, JELLI_CMD_GIFT, 0u},
        {130u, JELLI_CMD_PLAY, 0u},     {220u, JELLI_CMD_REST, 0u}, {250u, JELLI_CMD_WAKE, 0u},
        {260u, JELLI_CMD_CLEAN, 0u},    {320u, JELLI_CMD_CARE, 0u}, {640u, JELLI_CMD_TRAVEL, 1u},
        {680u, JELLI_CMD_ACTIVATE, 2u}, {700u, JELLI_CMD_FEED, 0u}, {760u, JELLI_CMD_CLAIM, 0u}};
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        if (frame == steps[i].frame) {
            JelliCommand command = {steps[i].kind, engine->game.pets[engine->game.active].id,
                                    steps[i].value};
            engine->ui.result = jelli_game_command(&engine->game, command);
        }
    }
}

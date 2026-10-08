#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>

static void clock_state(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id)
{
    char body[256];
    int size =
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"clock_known\":%s,\"unix_seconds\":%" PRIu64
                 ",\"timezone_minutes\":%d,\"pending\":%s,\"result\":\"%s\"}",
                 engine->game.wall_known ? "true" : "false", engine->game.wall_seconds,
                 (int)engine->ui.timezone_minutes, engine->ui.wall_set_requested ? "true" : "false",
                 jelli_game_result_name(engine->ui.result));
    if (size > 0 && (size_t)size < sizeof(body))
        jelli_debug_response(debug, id, body);
}

void jelli_debug_clock(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                       unsigned count)
{
    if (count == 3u) {
        clock_state(debug, engine, id);
        return;
    }
    uint32_t seconds, offset;
    if (count != 5u || !jelli_debug_number(words[3], &seconds) ||
        !jelli_debug_number(words[4], &offset) || seconds < UINT32_C(946684800) ||
        seconds >= UINT32_C(4102444800) || offset > 1560u) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"range\"}");
        return;
    }
    if (debug->captured || engine->game.resuming || engine->ui.wall_set_requested) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    engine->ui.wall_set_seconds = seconds;
    engine->ui.wall_set_offset_minutes = (int16_t)((int)offset - 720);
    engine->ui.wall_set_requested = true;
    jelli_debug_response(debug, id, "{\"ok\":true,\"pending\":true}");
}

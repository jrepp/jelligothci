#include "debug_internal.h"
#include "jelli/pet_effect.h"
#include <string.h>

void jelli_debug_effect(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                        unsigned count)
{
    if (!((count == 4u && !strcmp(words[3], "stop")) ||
          (count == 5u && !strcmp(words[3], "play")))) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"syntax\"}");
        return;
    }
    if (debug->captured || engine->game.resuming || engine->paused || engine->ui.menu_open) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    if (!strcmp(words[3], "stop")) {
        engine->celebrations.pending = 0u;
        jelli_pet_effect_stop(&engine->ui);
    } else if (!jelli_pet_effect_start(&engine->ui, words[4], engine->animation_ms)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"unknown_effect\"}");
        return;
    }
    jelli_debug_response(debug, id, "{\"ok\":true}");
}

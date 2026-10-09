#include "debug_internal.h"
#include <string.h>

/* "app agent" asks the host to restart into the voice agent (RFC-003, ADR-013). The
 * core gates the request and asks for a checkpoint; the host owns partitions and
 * the restart, which it performs only after this reply and that checkpoint. */
void jelli_debug_app(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                     unsigned count)
{
    if (count != 4u || strcmp(words[3], "agent") != 0) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"app_syntax\"}");
        return;
    }
    /* A catch-up skips checkpoints, and a capture freezes the session; either would
     * restart without saving the latest progress. */
    if (debug->captured || engine->game.resuming) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    if (!debug->switch_app) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"app_unavailable\"}");
        return;
    }
    if (!debug->switch_app(debug->switch_app_ctx)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"agent_missing\"}");
        return;
    }
    engine->ui.save_requested = true;
    jelli_debug_response(debug, id, "{\"ok\":true,\"app\":\"agent\",\"restart\":true}");
}

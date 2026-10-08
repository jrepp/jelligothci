#include "debug_internal.h"
#include "jelli/sound.h"

void jelli_debug_sound(JelliDebug *debug, uint32_t id, char **words, unsigned count)
{
    uint32_t cue = 0, volume = 0;
    if (count != 5u || !jelli_debug_number(words[3], &cue) ||
        !jelli_debug_number(words[4], &volume) || cue >= JELLI_SOUND_COUNT || volume > 80u) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"sound_range\"}");
        return;
    }
    if (debug->captured) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    if (!debug->sound) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"sound_unavailable\"}");
        return;
    }
    bool queued = debug->sound(debug->sound_ctx, cue, volume);
    jelli_debug_response(debug, id,
                         queued ? "{\"ok\":true,\"sound\":\"queued\"}"
                                : "{\"ok\":false,\"error\":\"sound_busy_or_unavailable\"}");
}

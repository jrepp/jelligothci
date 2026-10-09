#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

size_t jelli_debug_screen_json(char *out, size_t capacity, const JelliPetEngine *engine,
                               uint64_t now)
{
    int size =
        snprintf(out, capacity, "{\"awake\":%s,\"sleep_in_ms\":%" PRIu64 ",\"sleep_after_ms\":%u}",
                 jelli_pet_screen_awake(engine) ? "true" : "false",
                 jelli_pet_screen_remaining(&engine->screen, now), JELLI_SCREEN_SLEEP_MS);
    return size < 0 || (size_t)size >= capacity ? 0u : (size_t)size;
}

/* "screen" reports; "screen sleep" and "screen wake" force a transition. Forced
 * transitions behave like real ones: the frame stops presenting, or the next
 * frame repaints everything and the idle timer restarts. */
void jelli_debug_screen(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                        unsigned count, uint64_t now)
{
    bool sleep = count == 4u && strcmp(words[3], "sleep") == 0;
    bool wake = count == 4u && strcmp(words[3], "wake") == 0;
    if (count != 3u && !sleep && !wake) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"screen_action\"}");
        return;
    }
    if (count != 3u && debug->captured) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    if (sleep)
        jelli_pet_screen_sleep(engine, now);
    else if (wake)
        jelli_pet_screen_wake(engine, now);
    char body[128];
    static const char prefix[] = "{\"ok\":true,\"screen\":";
    memcpy(body, prefix, sizeof(prefix) - 1u);
    size_t used = jelli_debug_screen_json(body + sizeof(prefix) - 1u, sizeof(body) - sizeof(prefix),
                                          engine, now);
    if (!used) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"capacity\"}");
        return;
    }
    used += sizeof(prefix) - 1u;
    body[used++] = '}';
    body[used] = '\0';
    jelli_debug_response(debug, id, body);
}

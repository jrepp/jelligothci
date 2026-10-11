#ifndef JELLI_DEBUG_H
#define JELLI_DEBUG_H

#include "jelli/pet_engine.h"
#include "jelli/debug_protocol.h"

/* Shared discovery list for game hosts; adapters append their own commands. */
#define JELLI_DEBUG_GAME_COMMANDS_JSON                                                             \
    "\"capabilities\",\"state\",\"capture\",\"pixels\",\"release\",\"clock\","                     \
    "\"habits\",\"sleep-log\",\"events\",\"sound\",\"tunables\",\"tune\","                         \
    "\"cheat\",\"press\",\"swipe\",\"tap\""

#define JELLI_DEBUG_IDLE_MS 5000u
#define JELLI_DEBUG_CAPTURE_MS 30000u

typedef struct JelliDebug JelliDebug;
struct JelliDebug {
    JelliDebugProtocol protocol;
    uint64_t capture_start, capture_activity;
    uint32_t capture_id;
    bool captured;
    /* Optional host-owned, nonblocking sound queue; called on engine thread. */
    bool (*sound)(void *ctx, unsigned cue, unsigned volume);
    void *sound_ctx;
    /* Optional bounded host extension; engine thread, no blocking I/O. */
    bool (*command)(void *ctx, JelliDebug *debug, const JelliPetEngine *engine, uint32_t id,
                    char **words, unsigned count);
    void *command_ctx;
};

/* Zero-initialize. Call only on the engine thread, between frames. No IO/heap.
 * Feed one byte only when reply_size == 0. Host sends reply, then clears size.
 * Malformed/oversized lines are discarded through LF. One outstanding request.
 * Capture borrows the existing framebuffer: skip frames while frozen returns
 * true, discard physical input, and call frozen every host iteration. Frozen
 * time is forgiven; expiry/release never causes simulation catch-up. */
void jelli_debug_response(JelliDebug *debug, uint32_t id, const char *body);
void jelli_debug_feed(JelliDebug *debug, JelliPetEngine *engine, char byte, uint64_t now);
bool jelli_debug_frozen(JelliDebug *debug, JelliPetEngine *engine, uint64_t now);

#endif

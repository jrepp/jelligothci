#ifndef JELLI_DEBUG_H
#define JELLI_DEBUG_H

#include "jelli/pet_engine.h"
#include <stddef.h>

#define JELLI_DEBUG_LINE 96u
#define JELLI_DEBUG_REPLY 4096u
#define JELLI_DEBUG_IDLE_MS 5000u
#define JELLI_DEBUG_CAPTURE_MS 30000u

typedef struct {
    char line[JELLI_DEBUG_LINE], reply[JELLI_DEBUG_REPLY];
    size_t used, reply_size;
    uint64_t capture_start, capture_activity;
    uint32_t capture_id;
    bool discard, captured;
    /* Optional host-owned, nonblocking sound queue; called on engine thread. */
    bool (*sound)(void *ctx, unsigned cue, unsigned volume);
    void *sound_ctx;
    /* Optional host app switch ("app agent"), called on the engine thread. Returns
     * true when a bootable agent exists and the host will restart into it after this
     * reply is sent and the requested checkpoint is written; false leaves the pet. */
    bool (*switch_app)(void *ctx);
    void *switch_app_ctx;
} JelliDebug;

/* Zero-initialize. Call only on the engine thread, between frames. No IO/heap.
 * Feed one byte only when reply_size == 0. Host sends reply, then clears size.
 * Malformed/oversized lines are discarded through LF. One outstanding request.
 * Capture borrows the existing framebuffer: skip frames while frozen returns
 * true, discard physical input, and call frozen every host iteration. Frozen
 * time is forgiven; expiry/release never causes simulation catch-up. */
void jelli_debug_feed(JelliDebug *debug, JelliPetEngine *engine, char byte, uint64_t now);
bool jelli_debug_frozen(JelliDebug *debug, JelliPetEngine *engine, uint64_t now);

#endif

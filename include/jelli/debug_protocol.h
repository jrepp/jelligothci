#ifndef JELLI_DEBUG_PROTOCOL_H
#define JELLI_DEBUG_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JELLI_DEBUG_LINE 512u
#define JELLI_DEBUG_REPLY 4096u

typedef struct {
    char line[JELLI_DEBUG_LINE], reply[JELLI_DEBUG_REPLY];
    size_t used, reply_size;
    bool discard;
} JelliDebugProtocol;

typedef void (*JelliDebugDispatch)(void *ctx, uint32_t id, char **words, unsigned count);
/* Zero-initialize; one owner and one outstanding reply. Dispatch borrows tokens
 * only until return. Host drains reply then clears reply_size. No IO or heap.
 * Malformed/oversized requests are dropped through LF; request storage is wiped. */
void jelli_debug_protocol_feed(JelliDebugProtocol *protocol, char byte, JelliDebugDispatch dispatch,
                               void *ctx);
void jelli_debug_protocol_response(JelliDebugProtocol *protocol, uint32_t id, const char *body);
bool jelli_debug_number(const char *text, uint32_t *out);
#endif

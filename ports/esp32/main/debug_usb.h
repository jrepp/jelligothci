#ifndef JELLI_DEBUG_USB_H
#define JELLI_DEBUG_USB_H
#include "jelli/debug_protocol.h"

/* One app/task owns USB. Callback consumes a byte synchronously, with no IO.
 * Shared by the normal and factory dispatchers. */
typedef void (*JelliDebugFeed)(JelliDebugProtocol *protocol, void *ctx, char byte, uint64_t now);
void jelli_debug_usb_init(void);
void jelli_debug_usb_poll(JelliDebugProtocol *protocol, JelliDebugFeed feed, void *ctx,
                          uint64_t now);
#endif

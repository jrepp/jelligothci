#ifndef JELLI_DEBUG_WIRE_H
#define JELLI_DEBUG_WIRE_H
#include "jelli/pet_engine.h"
void jelli_debug_wire_init(void);
/* Engine thread only. Returns true while capture owns the framebuffer. */
bool jelli_debug_wire_poll(JelliPetEngine *engine, uint64_t now);
/* Engine thread only. True while a reply still waits for the USB host. */
bool jelli_debug_wire_reply_pending(void);
#endif

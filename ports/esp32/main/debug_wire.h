#ifndef JELLI_DEBUG_WIRE_H
#define JELLI_DEBUG_WIRE_H
#include "jelli/pet_engine.h"
void jelli_debug_wire_init(void);
/* Engine thread only. Returns true while capture owns the framebuffer. */
bool jelli_debug_wire_poll(JelliPetEngine *engine, uint64_t now);
#endif

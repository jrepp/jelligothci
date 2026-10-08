#ifndef JELLI_DEBUG_SOCKET_H
#define JELLI_DEBUG_SOCKET_H
#include "jelli/pet_engine.h"
bool jelli_sdl_debug_open(const char *path);
bool jelli_sdl_debug_poll(JelliPetEngine *engine);
void jelli_sdl_debug_close(void);
#endif

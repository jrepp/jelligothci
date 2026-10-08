#ifndef JELLI_DEBUG_INTERNAL_H
#define JELLI_DEBUG_INTERNAL_H
#include "jelli/debug.h"
void jelli_debug_state(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id);
bool jelli_debug_number(const char *text, uint32_t *out);
void jelli_debug_response(JelliDebug *debug, uint32_t id, const char *body);
void jelli_debug_tunables(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                          unsigned count);
void jelli_debug_sound(JelliDebug *debug, uint32_t id, char **words, unsigned count);
void jelli_debug_events(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id, char **words,
                        unsigned count);
void jelli_debug_cheat(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                       unsigned count);
void jelli_debug_clock(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                       unsigned count);
void jelli_debug_habits(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id,
                        const char *command, unsigned count);
#endif

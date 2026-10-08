#ifndef JELLI_SDL_SESSION_H
#define JELLI_SDL_SESSION_H

#include "jelli/pet_engine.h"
#include "jelli/save.h"
#include "options.h"

typedef struct {
    const char *path;
    JelliSave snapshot;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    uint64_t sequence, last_save_ms, last_clock_ms, override_wall_ms, override_monotonic_ms,
        saved_ticks;
    bool needs_reconcile, clock_sampled, clock_override;
} JelliSession;

bool jelli_sdl_session_open(JelliSession *session, JelliPetEngine *engine,
                            const JelliOptions *options);
bool jelli_sdl_session_save(JelliSession *session, JelliPetEngine *engine,
                            const JelliOptions *options);
void jelli_sdl_session_clock(JelliSession *session, JelliPetEngine *engine,
                             const JelliOptions *options);
void jelli_sdl_session_update(JelliSession *session, JelliPetEngine *engine,
                              const JelliOptions *options);
void jelli_sdl_demo(JelliPetEngine *engine, unsigned long frame);

#endif

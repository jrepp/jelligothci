#ifndef JELLI_SDL_POWER_H
#define JELLI_SDL_POWER_H

#include "jelli/pet_engine.h"

#define JELLI_SDL_POWER_INTERVAL_MS 5000u

typedef struct {
    uint64_t last_ms;
    bool sampled;
} JelliSdlPower;

/* Samples SDL_GetPowerInfo at most once per interval of injected time and writes
 * the result into engine->ui.power on the engine thread. Disabled hosts (headless
 * runs) never sample, so their snapshots stay deterministic: the reading remains
 * unknown unless the debug override sets one. */
void jelli_sdl_power_poll(JelliSdlPower *power, JelliPetEngine *engine, bool enabled);

#endif

#ifndef JELLI_PET_ENGINE_H
#define JELLI_PET_ENGINE_H

#include "jelli/pet_ui.h"

typedef struct {
    JelliPlatform platform;
    JelliSurface surface;
    JelliGame game;
    JelliPetUi ui;
    uint64_t last_ms;
    uint32_t animation_ms;
    bool running, paused;
} JelliPetEngine;

/* Separate mode preserves the shapes diagnostic and its public contract. */
bool jelli_pet_init(JelliPetEngine *engine, JelliPlatform platform, JelliSurface surface);
bool jelli_pet_frame(JelliPetEngine *engine);

#endif

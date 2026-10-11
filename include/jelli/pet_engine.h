#ifndef JELLI_PET_ENGINE_H
#define JELLI_PET_ENGINE_H

#include "jelli/pet_ui.h"
#include "jelli/device.h"

typedef struct {
    JelliPlatform platform;
    JelliSurface surface;
    JelliMotionDriver motion_driver;
    JelliMotionSample motion;
    JelliDeviceResult motion_result;
    JelliDisplayDriver display_driver;
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    uint64_t last_ms;
    uint64_t animation_ms;
    bool running, paused;
} JelliPetEngine;

/* Separate mode preserves the shapes diagnostic and its public contract. */
bool jelli_pet_init(JelliPetEngine *engine, JelliPlatform platform, JelliSurface surface);
/* Bind optional providers after init. Contexts outlive their binding. NULL
 * callbacks disable a capability. Providers run on the engine calling thread. */
void jelli_pet_bind_devices(JelliPetEngine *engine, JelliMotionDriver motion,
                            JelliDisplayDriver display);
bool jelli_pet_frame(JelliPetEngine *engine);

#endif

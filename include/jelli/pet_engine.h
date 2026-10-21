#ifndef JELLI_PET_ENGINE_H
#define JELLI_PET_ENGINE_H

#include "jelli/pet_ui.h"
#include "jelli/device.h"
#include "jelli/pet_celebration.h"
#include "jelli/music.h"

/* Host owns audio state; callbacks are bounded and serialize with its renderer.
 * Command serials distinguish queued ESP commands from stale status snapshots. */
typedef struct {
    void *ctx;
    bool (*control)(void *ctx, JelliMusicCommand command);
    bool (*status)(void *ctx, JelliMusicStatus *status);
} JelliMusicDriver;

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
    JelliPetCelebration celebrations;
    uint64_t last_ms;
    uint64_t animation_ms;
    JelliMusicDriver music_driver;
    uint32_t music_serial, music_cursor, music_pet, music_generation;
    uint8_t music_volume, music_song;
    bool music_running;
    bool running, paused;
} JelliPetEngine;

/* Separate mode preserves the shapes diagnostic and its public contract. */
bool jelli_pet_init(JelliPetEngine *engine, JelliPlatform platform, JelliSurface surface);
/* Bind optional providers after init. Contexts outlive their binding. NULL
 * callbacks disable a capability. Providers run on the engine calling thread. */
void jelli_pet_bind_devices(JelliPetEngine *engine, JelliMotionDriver motion,
                            JelliDisplayDriver display);
void jelli_pet_bind_music(JelliPetEngine *engine, JelliMusicDriver driver);
/* Engine calls progress before inputs and sync after them; never from audio tasks. */
void jelli_pet_music_progress(JelliPetEngine *engine);
void jelli_pet_music_sync(JelliPetEngine *engine);
bool jelli_pet_frame(JelliPetEngine *engine);

#endif

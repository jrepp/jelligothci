#ifndef JELLI_SDL_SCENE_PREVIEW_H
#define JELLI_SDL_SCENE_PREVIEW_H

#include "jelli/pet_scene.h"
#include "options.h"

typedef struct {
    JelliScene scene;
    uint64_t last_ms;
    int32_t remainder;
    bool started, reset_held;
} JelliScenePreview;

bool jelli_sdl_scene_open(JelliScenePreview *preview, JelliPetUi *ui, const JelliOptions *options);
void jelli_sdl_scene_step(JelliScenePreview *preview, JelliPetUi *ui, const JelliOptions *options,
                          uint64_t now, bool paused);

#endif

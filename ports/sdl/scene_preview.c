#include "scene_preview.h"
#include <SDL.h>
#include <stdio.h>

bool jelli_sdl_scene_open(JelliScenePreview *preview, JelliPetUi *ui, const JelliOptions *options)
{
    if (!options->scene)
        return true;
    if (!jelli_pet_scene_select(ui, &preview->scene, options->scene)) {
        fprintf(stderr, "Scene unavailable: %s\nAvailable scenes:", options->scene);
        for (unsigned i = 0; i < jelli_scene_definition_count; ++i)
            fprintf(stderr, " %s", jelli_scene_definitions[i].key);
        fputc('\n', stderr);
        return false;
    }
    return true;
}

void jelli_sdl_scene_step(JelliScenePreview *preview, JelliPetUi *ui, const JelliOptions *options,
                          uint64_t now, bool paused)
{
    if (!options->scene)
        return;
    uint64_t elapsed = preview->started && now >= preview->last_ms ? now - preview->last_ms : 0;
    preview->last_ms = now;
    preview->started = true;
    if (paused)
        return;
    /* Diagnostic pacing: stalls never accumulate a travel backlog. */
    if (elapsed > 250u)
        elapsed = 250u;
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    bool focused = options->headless || SDL_GetKeyboardFocus() != NULL;
    int tx = options->tilt_x * JELLI_SCENE_TILT_ONE / 100;
    int ty = options->tilt_y * JELLI_SCENE_TILT_ONE / 100;
    int speed = options->travel_speed;
    if (focused && (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT]))
        tx = (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]) * JELLI_SCENE_TILT_ONE;
    if (focused && (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN]))
        ty = (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]) * JELLI_SCENE_TILT_ONE;
    if (focused && (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_D]))
        speed = (keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * 8;
    bool reset = focused && keys[SDL_SCANCODE_R];
    if (reset && !preview->reset_held) {
        (void)jelli_pet_scene_select(ui, &preview->scene, options->scene);
        preview->remainder = 0;
        elapsed = 0;
    }
    preview->reset_held = reset;
    int32_t distance = (int32_t)elapsed * speed * JELLI_SCENE_TILT_ONE + preview->remainder;
    preview->remainder = distance % 1000;
    jelli_scene_advance(&preview->scene, (int16_t)tx, (int16_t)ty, distance / 1000);
}

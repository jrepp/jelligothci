#include "jelli/sound.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "jelli/touch_guard.h"
#include "jelli/motion.h"
#include "jelli/engine.h"
#include "session.h"
#include "debug_socket.h"
#include "asset_reload.h"
#include "sound_output.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FRAME_MS = 8 };

typedef struct {
    JelliGesture gesture;
    JelliTouchGuard touch_guard;
    JelliMotionMailbox motion;
    JelliAssetReload *asset_reload;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    bool headless, failed, pet;
    uint64_t virtual_ms;
} Desktop;

/* Match the shared callback type without casting incompatible function pointers. */
// cppcheck-suppress constParameterCallback
static uint64_t now_ms(void *ctx)
{
    const Desktop *d = ctx;
    return d->headless ? d->virtual_ms : SDL_GetTicks64();
}
static bool poll_input(void *ctx, JelliInput *input)
{
    Desktop *d = ctx;
    SDL_Event event;
    for (unsigned n = 0; n < 32 && SDL_PollEvent(&event); ++n) {
        if (event.type == SDL_QUIT) {
            *input = (JelliInput){.kind = JELLI_QUIT};
            return true;
        }
        if (event.type == SDL_KEYDOWN && !event.key.repeat) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                *input = (JelliInput){.kind = JELLI_QUIT};
                return true;
            }
            if (event.key.keysym.sym == SDLK_SPACE) {
                *input = (JelliInput){.kind = JELLI_TOGGLE_PAUSE};
                return true;
            }
        }
        if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym == SDLK_m)
            jelli_motion_publish(&d->motion, JELLI_DEVICE_OK, (JelliMotionSample){now_ms(d), true});
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            /* SDL's logical-size renderer transforms mouse events for us. */
            uint8_t contacts = 1;
            jelli_touch_guard_apply(&d->touch_guard, false, &contacts);
            if (contacts)
                jelli_gesture_begin(&d->gesture, event.button.x, event.button.y);
        }
        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            uint8_t contacts = 0;
            jelli_touch_guard_apply(&d->touch_guard, false, &contacts);
            if (jelli_gesture_end(&d->gesture, event.button.x, event.button.y, input))
                return true;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            uint8_t contacts = 0;
            jelli_touch_guard_apply(&d->touch_guard, true, &contacts);
        }
    }
    return false;
}
static void present(void *ctx, const JelliSurface *s)
{
    Desktop *d = ctx;
    SDL_Rect rect = {(int)s->damage.x, (int)s->damage.y, (int)s->damage.width,
                     (int)s->damage.height};
    const uint16_t *pixels = s->pixels + s->damage.y * s->stride + s->damage.x;
    if ((s->damage.width && s->damage.height &&
         SDL_UpdateTexture(d->texture, &rect, pixels, (int)(s->stride * sizeof(uint16_t))) < 0) ||
        SDL_RenderClear(d->renderer) < 0 ||
        SDL_RenderCopy(d->renderer, d->texture, NULL, NULL) < 0) {
        fprintf(stderr, "Presentation failed: %s\n", SDL_GetError());
        d->failed = true;
        return;
    }
    SDL_RenderPresent(d->renderer);
}
static void paused(void *ctx, bool value)
{
    Desktop *d = ctx;
    SDL_SetWindowTitle(d->window,
                       value ? "Jelligotchi | paused | Space resumes"
                             : (d->pet ? "Jelligotchi | pet slice | Space pauses"
                                       : "Jelligotchi | shapes diagnostic | click changes colors"));
}
static bool create_display(Desktop *d)
{
    d->window = SDL_CreateWindow(
        d->pet ? "Jelligotchi | pet slice" : "Jelligotchi | shapes diagnostic",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, JELLI_WIDTH, JELLI_HEIGHT,
        d->headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!d->window)
        return false;
    if (!d->headless)
        d->renderer = SDL_CreateRenderer(d->window, -1, SDL_RENDERER_ACCELERATED);
    if (!d->renderer)
        d->renderer = SDL_CreateRenderer(d->window, -1, SDL_RENDERER_SOFTWARE);
    if (!d->renderer || SDL_RenderSetLogicalSize(d->renderer, JELLI_WIDTH, JELLI_HEIGHT) < 0)
        return false;
    d->texture = SDL_CreateTexture(d->renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                   JELLI_WIDTH, JELLI_HEIGHT);
    return d->texture != NULL;
}

static void play_feedback(JelliPetEngine *pet, uint64_t now, bool enabled)
{
    if (!enabled || pet->paused)
        return;
    unsigned cue = jelli_pet_ui_sound(&pet->ui, &pet->game.pets[pet->game.active], now);
    if (cue && pet->game.volume)
        (void)jelli_sdl_sound_request(NULL, cue - 1u,
                                      jelli_sound_volume(cue - 1u, pet->game.volume));
}

static void prepare_frame(Desktop *d, JelliPetEngine *pet, const JelliOptions *options,
                          JelliSession *session, uint64_t now, unsigned long frame, bool frozen)
{
    if (d->pet && !frozen) {
        jelli_sdl_asset_reload_poll(d->asset_reload, pet, now);
        jelli_sdl_session_clock(session, pet, options);
    }
    if (options->demo && !frozen)
        jelli_sdl_demo(pet, frame);
}

static void run_frames(JelliEngine *shapes, JelliPetEngine *pet, Desktop *d,
                       const JelliOptions *options, JelliSession *session)
{
    for (unsigned long frame = 0; !options->max_frames || frame < options->max_frames; ++frame) {
        uint64_t start = SDL_GetTicks64();
        /* A socket can connect during startup; serve it only after a real frame exists. */
        bool frozen = d->pet && pet->ui.rendered && jelli_sdl_debug_poll(pet);
        prepare_frame(d, pet, options, session, start, frame, frozen);
        bool running =
            d->pet ? (frozen ? pet->running : jelli_pet_frame(pet)) : jelli_frame(shapes);
        play_feedback(pet, start, d->pet && !frozen);
        if (!running || d->failed)
            break;
        if (d->pet && !frozen)
            jelli_sdl_session_update(session, pet, options);
        if (d->headless)
            d->virtual_ms += options->demo ? 100u : FRAME_MS;
        if (!d->headless || options->debug_socket) {
            uint64_t elapsed = SDL_GetTicks64() - start;
            if (elapsed < FRAME_MS)
                SDL_Delay((Uint32)(FRAME_MS - elapsed));
        }
    }
}

static bool save_snapshot(uint16_t *pixels, const char *path)
{
    SDL_Surface *bmp = SDL_CreateRGBSurfaceWithFormatFrom(pixels, JELLI_WIDTH, JELLI_HEIGHT, 16,
                                                          JELLI_WIDTH * 2, SDL_PIXELFORMAT_RGB565);
    if (!bmp)
        return false;
    int saved = SDL_SaveBMP(bmp, path);
    SDL_FreeSurface(bmp);
    return saved == 0;
}

static void open_audio(const Desktop *d, const JelliOptions *options)
{
    if (d->pet && (!d->headless || options->audio) && !jelli_sdl_sound_open())
        fprintf(stderr, "Sound unavailable: %s\n", SDL_GetError());
}

int main(int argc, char **argv)
{
    JelliOptions options;
    int parsed = jelli_sdl_options(argc, argv, &options);
    if (parsed >= 0)
        return parsed;
    Desktop d = {.headless = options.headless, .pet = options.pet};
    d.touch_guard.gesture = &d.gesture;
    if (d.headless)
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    int result = 1;
    uint16_t *pixels = calloc(JELLI_WIDTH * JELLI_HEIGHT, sizeof(*pixels));
    if (!pixels) {
        fprintf(stderr, "Framebuffer allocation failed\n");
        goto cleanup;
    }
    d.asset_reload = jelli_sdl_asset_reload_open(options.asset_pack);
    if (options.asset_pack && !d.asset_reload) {
        fprintf(stderr, "Live artwork workspace allocation failed\n");
        goto cleanup;
    }
    if (!create_display(&d))
        goto sdl_error;
    open_audio(&d, &options);
    if (!jelli_sdl_debug_open(options.debug_socket))
        goto cleanup;
    JelliSurface surface = {
        .pixels = pixels, .width = JELLI_WIDTH, .height = JELLI_HEIGHT, .stride = JELLI_WIDTH};
    JelliPlatform platform = {&d, now_ms, poll_input, present, paused};
    static JelliEngine shapes;
    static JelliPetEngine pet;
    static JelliSession session;
    if (d.pet) {
        if (!jelli_pet_init(&pet, platform, surface) ||
            !jelli_sdl_session_open(&session, &pet, &options))
            goto cleanup;
        jelli_pet_bind_devices(&pet, (JelliMotionDriver){&d.motion, jelli_motion_poll},
                               (JelliDisplayDriver){0});
    } else if (!jelli_init(&shapes, platform, surface))
        goto cleanup;
    run_frames(&shapes, &pet, &d, &options, &session);
    if (d.pet && !jelli_sdl_session_save(&session, &pet, &options))
        goto cleanup;
    if (d.failed)
        goto cleanup;
    if (options.snapshot && !save_snapshot(pixels, options.snapshot))
        goto sdl_error;
    result = 0;
    goto cleanup;
sdl_error:
    fprintf(stderr, "SDL error: %s\n", SDL_GetError());
cleanup:
    jelli_sdl_debug_close();
    jelli_sdl_sound_close();
    SDL_DestroyTexture(d.texture);
    SDL_DestroyRenderer(d.renderer);
    SDL_DestroyWindow(d.window);
    free(d.asset_reload);
    free(pixels);
    SDL_Quit();
    return result;
}

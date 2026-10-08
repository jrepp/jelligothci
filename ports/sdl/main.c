#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "jelli/engine.h"
#include "session.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FRAME_MS = 8 };

typedef struct {
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
    (void)ctx;
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
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            /* SDL's logical-size renderer transforms mouse events for us. */
            *input = (JelliInput){JELLI_TAP, event.button.x, event.button.y};
            return true;
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

static void run_frames(JelliEngine *shapes, JelliPetEngine *pet, Desktop *d,
                       const JelliOptions *options, JelliSession *session)
{
    for (unsigned long frame = 0; !options->max_frames || frame < options->max_frames; ++frame) {
        uint64_t start = SDL_GetTicks64();
        if (options->demo)
            jelli_sdl_demo(pet, frame);
        bool running = d->pet ? jelli_pet_frame(pet) : jelli_frame(shapes);
        if (!running || d->failed)
            break;
        if (d->pet)
            jelli_sdl_session_update(session, pet, options);
        if (d->headless)
            d->virtual_ms += options->demo ? 100u : FRAME_MS;
        else {
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

int main(int argc, char **argv)
{
    JelliOptions options;
    int parsed = jelli_sdl_options(argc, argv, &options);
    if (parsed >= 0)
        return parsed;
    Desktop d = {.headless = options.headless, .pet = options.pet};
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
    if (!create_display(&d))
        goto sdl_error;
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
    SDL_DestroyTexture(d.texture);
    SDL_DestroyRenderer(d.renderer);
    SDL_DestroyWindow(d.window);
    free(pixels);
    SDL_Quit();
    return result;
}

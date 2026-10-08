#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "jelli/engine.h"
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
    bool headless, failed;
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
                       value ? "Jelligotchi | paused | Space resumes | click changes colors"
                             : "Jelligotchi | shapes MVP | Space pauses | click changes colors");
}
static void usage(const char *name)
{
    printf("Usage: %s [--headless] [--frames N] [--snapshot file.bmp]\n"
           "Left click: 300 ms color transition. Space: pause motion. Escape: quit.\n"
           "Headless mode uses a deterministic injected clock (8 ms/frame).\n",
           name);
}
typedef struct {
    bool headless;
    unsigned long max_frames;
    const char *snapshot;
} Options;

/* Return -1 to continue, otherwise the requested process exit status. */
static int parse_options(int argc, char **argv, Options *options)
{
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headless"))
            options->headless = true;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end = NULL;
            const char *arg = argv[++i];
            errno = 0;
            options->max_frames = strtoul(arg, &end, 10);
            if (errno || !*arg || *end || *arg == '-' || !options->max_frames ||
                options->max_frames > UINT_MAX) {
                fprintf(stderr, "--frames requires a positive integer <= %u\n", (unsigned)UINT_MAX);
                return 2;
            }
        } else if (!strcmp(argv[i], "--snapshot") && i + 1 < argc)
            options->snapshot = argv[++i];
        else if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (options->headless && !options->max_frames)
        options->max_frames = 1;
    return -1;
}

static bool create_display(Desktop *d)
{
    d->window = SDL_CreateWindow(
        "Jelligotchi | shapes MVP | Space pauses | click changes colors", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, JELLI_WIDTH, JELLI_HEIGHT,
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

static void run_frames(JelliEngine *engine, Desktop *d, unsigned long max_frames)
{
    for (unsigned long frame = 0; !max_frames || frame < max_frames; ++frame) {
        uint64_t start = SDL_GetTicks64();
        if (!jelli_frame(engine) || d->failed)
            break;
        if (d->headless)
            d->virtual_ms += FRAME_MS;
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
    Options options = {0};
    int parsed = parse_options(argc, argv, &options);
    if (parsed >= 0)
        return parsed;
    Desktop d = {.headless = options.headless};
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
    JelliEngine engine;
    if (!jelli_init(&engine, platform, surface))
        goto cleanup;
    run_frames(&engine, &d, options.max_frames);
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

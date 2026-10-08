#ifndef JELLI_SDL_OPTIONS_H
#define JELLI_SDL_OPTIONS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool headless, pet, demo;
    unsigned long max_frames;
    const char *snapshot, *save_path;
    uint64_t wall_ms;
} JelliOptions;

/* -1 means continue startup; 0/2 are command-line exit statuses. */
int jelli_sdl_options(int argc, char **argv, JelliOptions *options);

#endif

#ifndef JELLI_SDL_SOUND_OUTPUT_H
#define JELLI_SDL_SOUND_OUTPUT_H
#include <stdbool.h>
bool jelli_sdl_sound_open(void);
void jelli_sdl_sound_close(void);
bool jelli_sdl_sound_request(void *ctx, unsigned cue, unsigned volume);
#endif

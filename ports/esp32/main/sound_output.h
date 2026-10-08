#ifndef JELLI_SOUND_OUTPUT_H
#define JELLI_SOUND_OUTPUT_H
#include <stdbool.h>
bool jelli_sound_output_init(void);
bool jelli_sound_output_request(void *ctx, unsigned cue, unsigned volume);
#endif

#ifndef JELLI_SOUND_OUTPUT_H
#define JELLI_SOUND_OUTPUT_H
#include <stdbool.h>
bool jelli_sound_output_init(void);
bool jelli_sound_output_request(void *ctx, unsigned cue, unsigned volume);
/* Engine task diagnostic; serializes close/open with the sound worker. */
bool jelli_sound_output_enable(bool enabled);
#endif

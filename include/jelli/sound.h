#ifndef JELLI_SOUND_H
#define JELLI_SOUND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JELLI_SOUND_RATE 22050u
#define JELLI_SOUND_BLOCK 512u
#define JELLI_SOUND_COUNT 7u

typedef struct {
    uint32_t position, length;
    int32_t dc;
    uint16_t phase, formant_a, formant_b;
    uint8_t cue, note;
    bool playing;
} JelliSynth;

/* Original fixed programs: chirp, happy, sparkle, hello, sleepy, tap, coo. "Hello" and
 * "sleepy" are vowel-like pet voices, not intelligible text-to-speech. */
const char *jelli_sound_name(unsigned cue);
bool jelli_sound_start(JelliSynth *synth, unsigned cue);
/* Mono signed native-endian PCM16. At most 512 samples/call; no IO, clock,
 * heap, floating point or hidden state. Returns samples produced, zero at end.
 * Caller owns synth/output and must serialize access. */
size_t jelli_sound_render(JelliSynth *synth, int16_t *output, size_t capacity);
#endif

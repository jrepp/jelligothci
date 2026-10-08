#ifndef JELLI_SOUND_H
#define JELLI_SOUND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JELLI_SOUND_RATE 22050u
#define JELLI_SOUND_BLOCK 512u
#define JELLI_SOUND_COUNT 7u
/* Zero-based cue index of the short UI tap; the other cues are pet voices and jingles. */
#define JELLI_SOUND_TAP 5u
/* Settings sound output level: 0 (muted) ..10, default the middle of the range. */
#define JELLI_SOUND_LEVEL_MAX 10u
#define JELLI_SOUND_LEVEL_DEFAULT 5u
/* Host output requests accept volumes 0..JELLI_SOUND_VOLUME_MAX. */
#define JELLI_SOUND_VOLUME_MAX 80u

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
/* Host request volume for a zero-based cue at a Settings level. The default level
 * reproduces the original fixed volumes (tap 25, other cues 18); each step scales
 * them linearly with rounding, so level 10 doubles them (at most 50) and level 0
 * returns 0, meaning "do not request". Levels above the maximum are clamped; an
 * unknown cue returns 0. The result never exceeds JELLI_SOUND_VOLUME_MAX. */
unsigned jelli_sound_volume(unsigned level, unsigned cue);
#endif

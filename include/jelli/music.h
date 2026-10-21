#ifndef JELLI_MUSIC_H
#define JELLI_MUSIC_H
#include "jelli/sound.h"

#define JELLI_MUSIC_TRACK_COUNT 7u
#define JELLI_MUSIC_RATE JELLI_SOUND_RATE
#define JELLI_MUSIC_SAMPLES (JELLI_MUSIC_RATE * 30u)

typedef enum {
    JELLI_MUSIC_START,
    JELLI_MUSIC_PAUSE,
    JELLI_MUSIC_RESUME,
    JELLI_MUSIC_STOP,
    JELLI_MUSIC_SEEK,
    JELLI_MUSIC_VOLUME
} JelliMusicAction;

typedef struct {
    JelliMusicAction action;
    uint32_t cursor, serial;
    uint8_t volume, song;
} JelliMusicCommand;

typedef struct {
    uint32_t cursor, serial;
    bool playing, finished;
} JelliMusicStatus;

typedef struct {
    uint32_t cursor, serial;
    uint8_t volume, song;
    bool playing;
} JelliMusic;

/* Seven original tracks. START atomically selects song 0..TRACK_COUNT-1.
 * START/SEEK cursor is an exact sample offset.
 * START/VOLUME use master volume 0..100. Other fields are ignored for other actions.
 * serial is a caller token echoed after the command is applied (including mute).
 * Invalid commands leave state unchanged. STOP rewinds; PAUSE retains position.
 * Caller owns serialization; no heap, IO or clock. Zero volume still advances. */
bool jelli_music_control(JelliMusic *music, JelliMusicCommand command);
JelliMusicStatus jelli_music_status(const JelliMusic *music);
/* Writes at most JELLI_SOUND_BLOCK samples; returns produced samples. Seek is
 * exact with bounded span lookup; no replay or hidden oscillator state. */
size_t jelli_music_render(JelliMusic *music, int16_t *output, size_t capacity);
/* Existing care cues keep their gain; music ducks while any care cue is active.
 * Samples are signed PCM16; saturation makes every input combination defined. */
int16_t jelli_music_mix(int16_t music, int16_t cue, unsigned cue_volume, bool duck);
/* Host-only 64-sample (~2.9 ms) transition from the last submitted music
 * sample. Apply before care mixing; ramps never advance the record cursor. */
typedef struct {
    int16_t last, origin;
    uint8_t remaining;
} JelliMusicTransition;
void jelli_music_transition_begin(JelliMusicTransition *transition);
int16_t jelli_music_transition_sample(JelliMusicTransition *transition, int16_t sample);
#endif

#include "jelli/music.h"
#include "music_samples.h"
#include <limits.h>

/* Fifteen bars at 120 BPM, eighth-note grid. The four original motifs develop
 * over C / A minor / F / G, then end on C. Pitch phases are Q16 sample indices. */
static const uint8_t melody[4][8] = {{0, 2, 4, 7, 4, 2, 1, 2},
                                     {5, 4, 2, 0, 2, 4, 5, 7},
                                     {3, 5, 7, 9, 7, 5, 4, 2},
                                     {4, 6, 7, 6, 4, 2, 1, 0}};
static const uint32_t key_step[] = {99528,  111719, 125402, 132859, 149127,
                                    167394, 187895, 199056, 223438, 250804};
static const uint32_t bass_step[] = {12441, 20924, 16607, 18641};

bool jelli_music_control(JelliMusic *music, JelliMusicCommand command)
{
    if (!music || command.action < JELLI_MUSIC_START || command.action > JELLI_MUSIC_VOLUME)
        return false;
    if (((command.action == JELLI_MUSIC_START || command.action == JELLI_MUSIC_SEEK) &&
         command.cursor > JELLI_MUSIC_SAMPLES) ||
        ((command.action == JELLI_MUSIC_START || command.action == JELLI_MUSIC_VOLUME) &&
         command.volume > 100u))
        return false;
    switch (command.action) {
    case JELLI_MUSIC_START:
        music->cursor = command.cursor;
        music->volume = command.volume;
        music->playing = music->cursor < JELLI_MUSIC_SAMPLES;
        break;
    case JELLI_MUSIC_PAUSE:
        music->playing = false;
        break;
    case JELLI_MUSIC_RESUME:
        music->playing = music->cursor < JELLI_MUSIC_SAMPLES;
        break;
    case JELLI_MUSIC_STOP:
        music->cursor = 0;
        music->playing = false;
        break;
    case JELLI_MUSIC_SEEK:
        music->cursor = command.cursor;
        if (music->cursor == JELLI_MUSIC_SAMPLES)
            music->playing = false;
        break;
    case JELLI_MUSIC_VOLUME:
        music->volume = command.volume;
        break;
    }
    music->serial = command.serial;
    return true;
}

JelliMusicStatus jelli_music_status(const JelliMusic *music)
{
    return music ? (JelliMusicStatus){music->cursor, music->serial, music->playing,
                                      music->cursor == JELLI_MUSIC_SAMPLES}
                 : (JelliMusicStatus){0};
}

static unsigned envelope(uint32_t age, unsigned duration)
{
    if (age < 64u)
        return age * 4u;
    if (age >= duration)
        return 0;
    /* Soft attack, exponential-like quadratic decay, finite release. */
    unsigned left = (duration - age) * 256u / duration;
    return left * left / 256u;
}

static int16_t sample(uint32_t cursor)
{
    uint32_t step = cursor * 4u / JELLI_MUSIC_RATE;
    uint32_t start = (step * JELLI_MUSIC_RATE + 3u) / 4u;
    uint32_t age = cursor - start;
    unsigned bar = step / 8u, motif = bar == 14u ? 0u : bar % 4u;
    unsigned note = bar == 14u && step % 8u >= 4u ? 0u : melody[motif][step % 8u];
    unsigned key = (unsigned)(((uint64_t)age * key_step[note] >> 16u) & 127u);
    int32_t value = (int32_t)jelli_music_keys[key] * (int32_t)envelope(age, 5300) / 4;
    uint32_t bass_age = cursor - ((step / 2u * JELLI_MUSIC_RATE + 1u) / 2u);
    unsigned bass = (unsigned)(((uint64_t)bass_age * bass_step[motif] >> 16u) & 63u);
    value += (int32_t)jelli_music_bass[bass] * (int32_t)envelope(bass_age, 10500) / 6;
    if (age < 768u && bar < 14u)
        value += (int32_t)jelli_music_brush[age / 3u] * (int32_t)(step % 2u ? 10 : 5);
    uint32_t remaining = JELLI_MUSIC_SAMPLES - cursor;
    if (remaining < JELLI_MUSIC_RATE)
        value = value * (int32_t)remaining / (int32_t)JELLI_MUSIC_RATE;
    return (int16_t)value; /* Three bounded voices sum to less than 12,000. */
}

size_t jelli_music_render(JelliMusic *music, int16_t *output, size_t capacity)
{
    if (!music || !output || !music->playing || music->cursor >= JELLI_MUSIC_SAMPLES)
        return 0;
    size_t count = capacity < JELLI_SOUND_BLOCK ? capacity : JELLI_SOUND_BLOCK;
    uint32_t remaining = JELLI_MUSIC_SAMPLES - music->cursor;
    if (count > remaining)
        count = remaining;
    for (size_t i = 0; i < count; ++i) {
        output[i] = (int16_t)((int32_t)sample(music->cursor) * music->volume / 100);
        ++music->cursor;
    }
    if (music->cursor == JELLI_MUSIC_SAMPLES)
        music->playing = false;
    return count;
}

int16_t jelli_music_mix(int16_t music, int16_t cue, unsigned cue_volume, bool duck)
{
    unsigned volume = cue_volume > 80u ? 80u : cue_volume;
    int32_t value = (duck ? music / 2 : music) + (int32_t)cue * (int32_t)volume / 100;
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

void jelli_music_transition_begin(JelliMusicTransition *transition)
{
    transition->origin = transition->last;
    transition->remaining = 64;
}

int16_t jelli_music_transition_sample(JelliMusicTransition *transition, int16_t input)
{
    int32_t value = input;
    if (transition->remaining) {
        value = ((int32_t)transition->origin * transition->remaining +
                 (int32_t)input * (64 - transition->remaining)) /
                64;
        --transition->remaining;
    }
    transition->last = (int16_t)value;
    return transition->last;
}

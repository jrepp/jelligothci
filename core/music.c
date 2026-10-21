#include "jelli/music.h"
#include <limits.h>

#include "music_data.h"

_Static_assert(sizeof(music_scores) / sizeof(music_scores[0]) == JELLI_MUSIC_TRACK_COUNT,
               "Generated song catalog must match the public track count");

bool jelli_music_control(JelliMusic *music, JelliMusicCommand command)
{
    if (!music || command.action < JELLI_MUSIC_START || command.action > JELLI_MUSIC_VOLUME)
        return false;
    if (((command.action == JELLI_MUSIC_START || command.action == JELLI_MUSIC_SEEK) &&
         command.cursor > JELLI_MUSIC_SAMPLES) ||
        ((command.action == JELLI_MUSIC_START || command.action == JELLI_MUSIC_VOLUME) &&
         command.volume > 100u) ||
        (command.action == JELLI_MUSIC_START && command.song >= JELLI_MUSIC_TRACK_COUNT))
        return false;
    switch (command.action) {
    case JELLI_MUSIC_START:
        music->cursor = command.cursor;
        music->volume = command.volume;
        music->song = command.song;
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

/* At most 3061 spans/channel. Find once per render block, never replay. */
static uint16_t find_span(const MusicChannel *channel, uint32_t cursor)
{
    uint16_t low = 0, high = channel->count;
    while ((unsigned)high - low > 1u) {
        uint16_t middle = (uint16_t)(low + (high - low) / 2u);
        if (channel->spans[middle].start <= cursor)
            low = middle;
        else
            high = middle;
    }
    return low;
}

static int32_t voice_sample(const MusicScore *score, const MusicSpan *span, uint32_t cursor)
{
    if (!span->instrument || !span->step)
        return 0;
    const MusicSample *sample = &score->samples[span->instrument - 1u];
    uint64_t phase = span->phase + (uint64_t)(cursor - span->start) * span->step;
    uint32_t offset = (uint32_t)(phase >> 16u);
    /* ProTracker initially plays the full sample for a loop at offset zero;
     * a nonzero loop start truncates that first pass at the loop end. */
    uint32_t first_end =
        sample->loop_start ? (uint32_t)sample->loop_start + sample->loop_length : sample->length;
    if (sample->loop_length && offset >= first_end)
        offset = sample->loop_start + (offset - first_end) % sample->loop_length;
    return offset < sample->length ? (int32_t)sample->pcm[offset] * span->volume : 0;
}

size_t jelli_music_render(JelliMusic *music, int16_t *output, size_t capacity)
{
    if (!music || !output || !music->playing || music->cursor >= JELLI_MUSIC_SAMPLES ||
        music->song >= JELLI_MUSIC_TRACK_COUNT)
        return 0;
    size_t count = capacity < JELLI_SOUND_BLOCK ? capacity : JELLI_SOUND_BLOCK;
    uint32_t remaining = JELLI_MUSIC_SAMPLES - music->cursor;
    if (count > remaining)
        count = remaining;
    const MusicScore *score = &music_scores[music->song];
    uint16_t indices[4];
    for (unsigned ch = 0; ch < 4u; ++ch)
        indices[ch] = find_span(&score->channels[ch], music->cursor);
    for (size_t i = 0; i < count; ++i) {
        int32_t value = 0;
        for (unsigned ch = 0; ch < 4u; ++ch) {
            const MusicChannel *channel = &score->channels[ch];
            if ((unsigned)indices[ch] + 1u < channel->count &&
                channel->spans[indices[ch] + 1u].start <= music->cursor)
                ++indices[ch];
            value += voice_sample(score, &channel->spans[indices[ch]], music->cursor);
        }
        /* Four PCM8 voices at volume 64 sum into PCM16 without clipping. */
        output[i] = (int16_t)(value * music->volume / 100);
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

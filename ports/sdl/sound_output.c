#include "sound_output.h"
#include "jelli/sound.h"
#include <SDL.h>
#include <string.h>

typedef struct {
    uint8_t cue, volume;
} Request;
static SDL_AudioDeviceID device;
static Request requests[4];
static unsigned read_at, write_at, queued, gain;
static JelliSynth synth;
static JelliMusic music;
static JelliMusicTransition transition;
static int16_t cue_pcm[JELLI_SOUND_BLOCK], music_pcm[JELLI_SOUND_BLOCK];

/* SDL serializes this callback with SDL_LockAudioDevice. All playback state
 * is owned by the callback; main controls or snapshots it only while locked. */
static void render_block(size_t count)
{
    memset(cue_pcm, 0, sizeof(cue_pcm));
    memset(music_pcm, 0, sizeof(music_pcm));
    if (!synth.playing && queued) {
        Request r = requests[read_at];
        read_at = (read_at + 1u) % 4u;
        --queued;
        gain = r.volume;
        (void)jelli_sound_start(&synth, r.cue);
    }
    bool duck = synth.playing;
    (void)jelli_sound_render(&synth, cue_pcm, count);
    (void)jelli_music_render(&music, music_pcm, count);
    for (size_t i = 0; i < count; ++i)
        music_pcm[i] = jelli_music_mix(jelli_music_transition_sample(&transition, music_pcm[i]),
                                       cue_pcm[i], gain, duck);
}

static void audio_callback(void *unused, Uint8 *stream, int length)
{
    (void)unused;
    if (length <= 0)
        return;
    memset(stream, 0, (size_t)length);
    size_t at = 0u, capacity = (size_t)length / sizeof(int16_t);
    while (at < capacity) {
        size_t count = capacity - at;
        if (count > JELLI_SOUND_BLOCK)
            count = JELLI_SOUND_BLOCK;
        render_block(count);
        memcpy(stream + at * sizeof(int16_t), music_pcm, count * sizeof(int16_t));
        at += count;
    }
}

bool jelli_sdl_music_control(void *ctx, JelliMusicCommand command)
{
    (void)ctx;
    if (!device)
        return false;
    SDL_LockAudioDevice(device);
    bool accepted = jelli_music_control(&music, command);
    if (accepted)
        jelli_music_transition_begin(&transition);
    SDL_UnlockAudioDevice(device);
    return accepted;
}

bool jelli_sdl_music_status(void *ctx, JelliMusicStatus *status)
{
    (void)ctx;
    if (!device || !status)
        return false;
    SDL_LockAudioDevice(device);
    *status = jelli_music_status(&music);
    SDL_UnlockAudioDevice(device);
    return true;
}

bool jelli_sdl_sound_open(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        return false;
    SDL_AudioSpec wanted = {.freq = (int)JELLI_SOUND_RATE,
                            .format = AUDIO_S16SYS,
                            .channels = 1,
                            .samples = JELLI_SOUND_BLOCK,
                            .callback = audio_callback};
    device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
    if (!device)
        return false;
    SDL_PauseAudioDevice(device, 0);
    return true;
}

bool jelli_sdl_sound_request(void *ctx, unsigned cue, unsigned volume)
{
    (void)ctx;
    if (!device || cue >= JELLI_SOUND_COUNT || volume > 80u)
        return false;
    SDL_LockAudioDevice(device);
    bool accepted = queued < 4u;
    if (accepted) {
        requests[write_at] = (Request){(uint8_t)cue, (uint8_t)volume};
        write_at = (write_at + 1u) % 4u;
        ++queued;
    }
    SDL_UnlockAudioDevice(device);
    return accepted;
}

void jelli_sdl_sound_close(void)
{
    if (device)
        SDL_CloseAudioDevice(device);
    device = 0;
    queued = read_at = write_at = 0;
    synth = (JelliSynth){0};
    music = (JelliMusic){0};
    transition = (JelliMusicTransition){0};
}

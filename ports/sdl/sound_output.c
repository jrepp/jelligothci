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

/* SDL serializes this callback with SDL_LockAudioDevice. All synth/queue state
 * is owned by the callback; main only enqueues while holding that lock. */
static void audio_callback(void *unused, Uint8 *stream, int length)
{
    (void)unused;
    if (length <= 0)
        return;
    memset(stream, 0, (size_t)length);
    /* memcpy avoids assumptions about the alignment of SDL's byte buffer. */
    int16_t block[JELLI_SOUND_BLOCK];
    size_t at = 0u, capacity = (size_t)length / sizeof(int16_t);
    while (at < capacity) {
        if (!synth.playing) {
            if (!queued)
                break;
            Request r = requests[read_at];
            read_at = (read_at + 1u) % 4u;
            --queued;
            gain = r.volume;
            (void)jelli_sound_start(&synth, r.cue);
        }
        size_t count = capacity - at;
        if (count > JELLI_SOUND_BLOCK)
            count = JELLI_SOUND_BLOCK;
        count = jelli_sound_render(&synth, block, count);
        if (!count)
            break;
        for (size_t i = 0; i < count; ++i)
            block[i] = (int16_t)((int32_t)block[i] * (int32_t)gain / 100);
        memcpy(stream + at * sizeof(int16_t), block, count * sizeof(int16_t));
        at += count;
    }
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
}

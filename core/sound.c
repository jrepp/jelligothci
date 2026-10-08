#include "jelli/sound.h"

/* The only waveform table: 128 bytes. Phase accumulators intentionally wrap. */
static const int16_t sine[64] = {
    0,      3212,   6393,   9512,   12539,  15446,  18204,  20787,  23170,  25329,  27245,
    28898,  30273,  31356,  32137,  32609,  32767,  32609,  32137,  31356,  30273,  28898,
    27245,  25329,  23170,  20787,  18204,  15446,  12539,  9512,   6393,   3212,   0,
    -3212,  -6393,  -9512,  -12539, -15446, -18204, -20787, -23170, -25329, -27245, -28898,
    -30273, -31356, -32137, -32609, -32767, -32609, -32137, -31356, -30273, -28898, -27245,
    -25329, -23170, -20787, -18204, -15446, -12539, -9512,  -6393,  -3212};

typedef struct {
    uint16_t start_hz, end_hz, duration_ms, vowel_a, vowel_b;
} Note;

static const Note programs[JELLI_SOUND_COUNT][5] = {
    {{880, 1760, 110, 0, 0}, {0, 0, 35, 0, 0}, {1319, 1976, 150, 0, 0}, {0}},
    {{1047, 1047, 100, 0, 0},
     {1319, 1319, 100, 0, 0},
     {1568, 1568, 120, 0, 0},
     {2093, 2093, 220, 0, 0},
     {0}},
    {{1319, 1568, 85, 0, 0},
     {1760, 2093, 85, 0, 0},
     {2093, 2637, 100, 0, 0},
     {2637, 3136, 180, 0, 0},
     {0}},
    {{280, 360, 140, 380, 1000},
     {360, 440, 160, 720, 1700},
     {0, 0, 50, 0, 0},
     {420, 500, 190, 320, 2300},
     {0}},
    {{360, 300, 220, 400, 1100}, {0, 0, 60, 0, 0}, {300, 230, 320, 500, 1250}, {0}},
    {{980, 1470, 65, 0, 0}, {0}},
    {{320, 410, 150, 360, 950}, {410, 280, 210, 400, 1100}, {0}}};
_Static_assert(sizeof(JelliSynth) <= 24u, "Synth state exceeds budget");

const char *jelli_sound_name(unsigned cue)
{
    static const char *const names[] = {"chirp",  "happy", "sparkle", "hello",
                                        "sleepy", "tap",   "coo"};
    return cue < JELLI_SOUND_COUNT ? names[cue] : NULL;
}

bool jelli_sound_start(JelliSynth *s, unsigned cue)
{
    if (!s || cue >= JELLI_SOUND_COUNT)
        return false;
    *s = (JelliSynth){.cue = (uint8_t)cue,
                      .playing = true,
                      .length = (uint32_t)programs[cue][0].duration_ms * JELLI_SOUND_RATE / 1000u};
    return true;
}

static int32_t triangle(uint16_t phase)
{
    int32_t ramp = phase < 32768u ? phase : 65535 - (int32_t)phase;
    return ramp * 2 - 32767;
}

static int32_t oscillator(JelliSynth *s, const Note *n)
{
    if (!n->start_hz)
        return 0;
    int32_t delta = (int32_t)n->end_hz - n->start_hz;
    uint32_t hz =
        (uint32_t)((int32_t)n->start_hz + delta * (int32_t)s->position / (int32_t)s->length);
    uint32_t phase = s->phase + hz * 65536u / JELLI_SOUND_RATE;
    s->phase = (uint16_t)phase;
    if (!n->vowel_a)
        return (triangle(s->phase) * 3 + sine[s->phase >> 10]) / 4;
    /* Damped formant bursts repeat at the fundamental: a tiny voiced source. */
    if (phase >= 65536u)
        s->formant_a = s->formant_b = 0u;
    s->formant_a = (uint16_t)(s->formant_a + (uint32_t)n->vowel_a * 65536u / JELLI_SOUND_RATE);
    s->formant_b = (uint16_t)(s->formant_b + (uint32_t)n->vowel_b * 65536u / JELLI_SOUND_RATE);
    int32_t voice = ((int32_t)sine[s->formant_a >> 10] * 3 + sine[s->formant_b >> 10]) / 4;
    return voice * (int32_t)((65535u - s->phase) >> 8) / 255;
}

static int16_t sample(JelliSynth *s)
{
    const Note *n = &programs[s->cue][s->note];
    uint32_t attack = s->position < 180u ? s->position * 256u / 180u : 256u;
    uint32_t remaining = s->length - s->position - 1u;
    uint32_t release = remaining < 600u ? remaining * 256u / 600u : 256u;
    int32_t envelope = (int32_t)(attack < release ? attack : release);
    int32_t raw = oscillator(s, n) * 6000 / 32768;
    s->dc += (raw - s->dc) / 128;
    /* Pause notes stay silent while the DC filter continues to settle. */
    int32_t value = n->start_hz ? (raw - s->dc) * envelope / 256 : 0;
    if (++s->position >= s->length) {
        ++s->note;
        s->position = 0;
        s->phase = s->formant_a = s->formant_b = 0u;
        s->length = (uint32_t)programs[s->cue][s->note].duration_ms * JELLI_SOUND_RATE / 1000u;
        s->playing = s->length != 0u;
    }
    return (int16_t)value;
}

size_t jelli_sound_render(JelliSynth *s, int16_t *output, size_t capacity)
{
    if (!s || !output || capacity > JELLI_SOUND_BLOCK || s->cue >= JELLI_SOUND_COUNT ||
        s->note >= 4u)
        return 0;
    size_t count = 0;
    while (count < capacity && s->playing)
        output[count++] = sample(s);
    return count;
}

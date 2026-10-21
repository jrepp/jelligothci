#include "jelli/sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static int16_t a[JELLI_SOUND_RATE], b[JELLI_SOUND_RATE];

static size_t render(unsigned cue, int16_t *output, size_t block)
{
    JelliSynth synth;
    CHECK(jelli_sound_start(&synth, cue));
    size_t count = 0u;
    while (synth.playing) {
        CHECK(count + block < JELLI_SOUND_RATE);
        size_t n = jelli_sound_render(&synth, output + count, block);
        CHECK(n > 0u);
        count += n;
    }
    CHECK(jelli_sound_render(&synth, output + count, block) == 0u);
    return count;
}

static void check_pause(unsigned cue, unsigned start_ms, unsigned duration_ms)
{
    size_t count = render(cue, a, JELLI_SOUND_BLOCK);
    size_t start = (size_t)start_ms * JELLI_SOUND_RATE / 1000u;
    size_t end = start + (size_t)duration_ms * JELLI_SOUND_RATE / 1000u;
    CHECK(end <= count);
    for (size_t i = start; i < end; ++i)
        CHECK(a[i] == 0);
}

static void subdued_rasp(void)
{
    CHECK(JELLI_SOUND_PET == 8u && JELLI_SOUND_RASP == 9u); /* Existing cue IDs stay stable. */
    size_t count = render(JELLI_SOUND_RASP, a, JELLI_SOUND_BLOCK);
    CHECK(count == 75u * JELLI_SOUND_RATE / 1000u);
    int rasp_step = 0;
    for (size_t i = 1u; i < count; ++i) {
        int step = abs((int)a[i] - a[i - 1u]);
        if (step > rasp_step)
            rasp_step = step;
    }
    count = render(JELLI_SOUND_BACK, b, JELLI_SOUND_BLOCK);
    int tap_step = 0;
    for (size_t i = 1u; i < count; ++i) {
        int step = abs((int)b[i] - b[i - 1u]);
        if (step > tap_step)
            tap_step = step;
    }
    CHECK(rasp_step > 0 && rasp_step < tap_step / 2);
    CHECK(strcmp(jelli_sound_name(JELLI_SOUND_RASP), "rasp") == 0);
}

int main(void)
{
    for (unsigned cue = 0; cue < JELLI_SOUND_COUNT; ++cue) {
        size_t count = render(cue, a, JELLI_SOUND_BLOCK);
        CHECK(render(cue, b, 7u) == count);
        CHECK(memcmp(a, b, count * sizeof(a[0])) == 0);
        CHECK(count >= JELLI_SOUND_RATE / 20u && count <= JELLI_SOUND_RATE);
        CHECK(a[0] == 0 && a[count - 1u] == 0);
        int peak = 0;
        int64_t sum = 0;
        for (size_t i = 0; i < count; ++i) {
            int magnitude = abs(a[i]);
            if (magnitude > peak)
                peak = magnitude;
            sum += a[i];
        }
        CHECK(peak > 1000 && peak < 12000);
        CHECK(llabs(sum / (int64_t)count) < 100);
        CHECK(jelli_sound_name(cue) != NULL);
    }
    subdued_rasp();
    check_pause(0u, 110u, 35u); /* Chirp. */
    check_pause(3u, 300u, 50u); /* Hello. */
    check_pause(4u, 220u, 60u); /* Sleepy. */
    JelliSynth synth = {0};
    CHECK(!jelli_sound_start(NULL, 0u));
    CHECK(!jelli_sound_start(&synth, JELLI_SOUND_COUNT));
    CHECK(jelli_sound_name(JELLI_SOUND_COUNT) == NULL);
    CHECK(jelli_sound_render(&synth, a, 512u) == 0u);
    CHECK(jelli_sound_start(&synth, 0u));
    CHECK(jelli_sound_render(&synth, a, 513u) == 0u && synth.position == 0u);
    CHECK(jelli_sound_render(&synth, NULL, 1u) == 0u);
    CHECK(jelli_sound_render(&synth, a, 0u) == 0u);
    puts("Sound block invariance, duration, envelopes, headroom, DC and bounds verified.");
    return 0;
}

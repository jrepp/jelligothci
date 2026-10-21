#include "jelli/music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static int16_t whole[JELLI_MUSIC_SAMPLES], split[JELLI_MUSIC_SAMPLES];

static void duration_and_partition(void)
{
    JelliMusic a = {0}, b = {0};
    JelliMusicCommand start = {.action = JELLI_MUSIC_START, .serial = 1, .volume = 100};
    CHECK(jelli_music_control(&a, start) && jelli_music_control(&b, start));
    const size_t length = sizeof(whole) / sizeof(whole[0]);
    size_t at = 0;
    while (a.playing)
        at += jelli_music_render(&a, whole + at, JELLI_SOUND_BLOCK);
    CHECK(at == length && jelli_music_status(&a).finished);
    CHECK(!jelli_music_render(&a, split, 1));
    at = 0;
    while (b.playing)
        at += jelli_music_render(&b, split + at, 1u + at % 509u);
    CHECK(at == length && !memcmp(whole, split, sizeof(whole)));
    bool nonzero = false;
    for (size_t i = 0; i < length; ++i) {
        CHECK(whole[i] > -12000 && whole[i] < 12000);
        nonzero = nonzero || whole[i] != 0;
    }
    CHECK(nonzero);
}

static void seek_and_controls(void)
{
    JelliMusic music = {0};
    int16_t block[JELLI_SOUND_BLOCK];
    const uint32_t positions[] = {0, 1, 5512, 5513, 22050, 330751, JELLI_MUSIC_SAMPLES - 1};
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        CHECK(jelli_music_control(&music, (JelliMusicCommand){.action = JELLI_MUSIC_START,
                                                              .cursor = positions[i],
                                                              .serial = i + 1u,
                                                              .volume = 100}));
        size_t count = jelli_music_render(&music, block, JELLI_SOUND_BLOCK);
        CHECK(count && !memcmp(block, whole + positions[i], count * sizeof(block[0])));
    }
    CHECK(
        jelli_music_control(&music, (JelliMusicCommand){.action = JELLI_MUSIC_START, .volume = 0}));
    CHECK(jelli_music_render(&music, block, 123) == 123 && music.cursor == 123);
    for (unsigned i = 0; i < 123u; ++i)
        CHECK(block[i] == 0);
    CHECK(jelli_music_control(&music,
                              (JelliMusicCommand){.action = JELLI_MUSIC_PAUSE, .serial = 20}));
    CHECK(!jelli_music_render(&music, block, 512) && music.cursor == 123);
    CHECK(jelli_music_status(&music).serial == 20);
    CHECK(jelli_music_control(&music,
                              (JelliMusicCommand){.action = JELLI_MUSIC_SEEK, .cursor = 22050}));
    CHECK(!music.playing && music.cursor == 22050);
    CHECK(jelli_music_control(&music, (JelliMusicCommand){.action = JELLI_MUSIC_RESUME}));
    CHECK(jelli_music_control(&music,
                              (JelliMusicCommand){.action = JELLI_MUSIC_VOLUME, .volume = 100}));
    CHECK(jelli_music_render(&music, block, 1) == 1 && block[0] == whole[22050]);
    CHECK(!jelli_music_control(
        &music, (JelliMusicCommand){.action = JELLI_MUSIC_SEEK, .cursor = UINT32_MAX}));
    CHECK(music.cursor == 22051);
    CHECK(!jelli_music_control(&music,
                               (JelliMusicCommand){.action = JELLI_MUSIC_VOLUME, .volume = 101}));
    CHECK(jelli_music_control(&music, (JelliMusicCommand){.action = JELLI_MUSIC_STOP}));
    CHECK(!music.playing && !music.cursor && !jelli_music_status(&music).finished);
    CHECK(jelli_music_mix(INT16_MAX, INT16_MAX, 100, false) == INT16_MAX);
    CHECK(jelli_music_mix(INT16_MIN, INT16_MIN, 100, false) == INT16_MIN);
    CHECK(jelli_music_mix(1000, 0, 0, true) == 500);
    CHECK(!jelli_music_render(NULL, block, 1) && !jelli_music_render(&music, NULL, 1));
}

static void output_bounds(void)
{
    JelliMusic music = {0};
    int16_t guarded[JELLI_SOUND_BLOCK + 2u] = {0};
    guarded[0] = 123;
    guarded[JELLI_SOUND_BLOCK + 1u] = 456;
    CHECK(jelli_music_control(&music,
                              (JelliMusicCommand){.action = JELLI_MUSIC_START, .volume = 100}));
    CHECK(jelli_music_render(&music, guarded + 1, 0) == 0 && music.cursor == 0);
    CHECK(jelli_music_render(&music, guarded + 1, JELLI_SOUND_BLOCK + 1u) == JELLI_SOUND_BLOCK);
    CHECK(guarded[0] == 123 && guarded[JELLI_SOUND_BLOCK + 1u] == 456);
    CHECK(!jelli_music_control(NULL, (JelliMusicCommand){0}));
    CHECK(music.cursor == JELLI_SOUND_BLOCK);
}

static void transitions(void)
{
    JelliMusicTransition transition = {.last = 32000};
    jelli_music_transition_begin(&transition);
    CHECK(jelli_music_transition_sample(&transition, -32000) == 32000);
    int16_t previous = 32000;
    for (unsigned i = 1; i < 64; ++i) {
        int16_t value = jelli_music_transition_sample(&transition, -32000);
        CHECK(value == previous - 1000);
        previous = value;
    }
    CHECK(!transition.remaining);
    CHECK(jelli_music_transition_sample(&transition, -32000) == -32000);
    jelli_music_transition_begin(&transition);
    for (unsigned i = 0; i < 64; ++i)
        CHECK(jelli_music_transition_sample(&transition, 0) == -32000 + (int)i * 500);
    CHECK(jelli_music_transition_sample(&transition, 0) == 0);
}

int main(void)
{
    duration_and_partition();
    seek_and_controls();
    output_bounds();
    transitions();
    printf("Music duration, partition, seek, control and clipping passed; state=%zu bytes\n",
           sizeof(JelliMusic));
    return 0;
}

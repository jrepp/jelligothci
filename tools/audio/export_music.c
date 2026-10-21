#include "jelli/music.h"
#include <stdio.h>

static bool integer(FILE *file, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        if (fputc((int)((value >> (i * 8u)) & 255u), file) == EOF)
            return false;
    return true;
}

static bool header(FILE *file)
{
    uint32_t bytes = JELLI_MUSIC_SAMPLES * 2u;
    return fwrite("RIFF", 1, 4, file) == 4 && integer(file, bytes + 36u, 4) &&
           fwrite("WAVEfmt ", 1, 8, file) == 8 && integer(file, 16, 4) && integer(file, 1, 2) &&
           integer(file, 1, 2) && integer(file, JELLI_MUSIC_RATE, 4) &&
           integer(file, JELLI_MUSIC_RATE * 2u, 4) && integer(file, 2, 2) && integer(file, 16, 2) &&
           fwrite("data", 1, 4, file) == 4 && integer(file, bytes, 4);
}

static bool song_index(const char *text, uint8_t *song)
{
    if (text[0] < '0' || text[0] > '9')
        return false;
    unsigned value = (unsigned)(text[0] - '0');
    if (text[1]) {
        if (text[1] < '0' || text[1] > '9' || text[2])
            return false;
        value = value * 10u + (unsigned)(text[1] - '0');
    }
    if (value >= JELLI_MUSIC_TRACK_COUNT)
        return false;
    *song = (uint8_t)value;
    return true;
}

int main(int argc, char **argv)
{
    uint8_t song = 0u;
    if (argc < 2 || argc > 3 || (argc == 3 && !song_index(argv[2], &song))) {
        fprintf(stderr, "Usage: jelli_music_export output.wav [song 0..%u]\n",
                JELLI_MUSIC_TRACK_COUNT - 1u);
        return 2;
    }
    FILE *file = fopen(argv[1], "wb");
    if (!file)
        return 1;
    JelliMusic music = {0};
    (void)jelli_music_control(
        &music, (JelliMusicCommand){.action = JELLI_MUSIC_START, .volume = 80, .song = song});
    int16_t block[JELLI_SOUND_BLOCK];
    bool ok = header(file);
    while (ok && music.playing) {
        size_t count = jelli_music_render(&music, block, JELLI_SOUND_BLOCK);
        for (size_t i = 0; ok && i < count; ++i)
            ok = integer(file, (uint16_t)block[i], 2);
    }
    bool closed = fclose(file) == 0;
    return ok && closed ? 0 : 1;
}

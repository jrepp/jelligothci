#include "jelli/sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (i * 8u));
}

static bool export_wav(FILE *file, unsigned cue)
{
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4u);
    memcpy(header + 8, "WAVEfmt ", 8u);
    le32(header + 16, 16u);
    header[20] = 1u;
    header[22] = 1u;
    le32(header + 24, JELLI_SOUND_RATE);
    le32(header + 28, JELLI_SOUND_RATE * 2u);
    header[32] = 2u;
    header[34] = 16u;
    memcpy(header + 36, "data", 4u);
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header))
        return false;
    JelliSynth synth;
    if (!jelli_sound_start(&synth, cue))
        return false;
    uint32_t bytes = 0;
    int16_t pcm[JELLI_SOUND_BLOCK];
    uint8_t encoded[JELLI_SOUND_BLOCK * 2u];
    while (synth.playing) {
        size_t count = jelli_sound_render(&synth, pcm, JELLI_SOUND_BLOCK);
        if (!count)
            return false;
        for (size_t i = 0; i < count; ++i) {
            uint16_t value = (uint16_t)pcm[i];
            encoded[i * 2u] = (uint8_t)value;
            encoded[i * 2u + 1u] = (uint8_t)(value >> 8);
        }
        if (fwrite(encoded, 2u, count, file) != count)
            return false;
        bytes += (uint32_t)count * 2u;
    }
    le32(header + 4, bytes + 36u);
    le32(header + 40, bytes);
    return fseek(file, 0, SEEK_SET) == 0 &&
           fwrite(header, 1, sizeof(header), file) == sizeof(header);
}

int main(int argc, char **argv)
{
    if (argc != 3 || strlen(argv[1]) != 1u || argv[1][0] < '0' ||
        (unsigned)(argv[1][0] - '0') >= JELLI_SOUND_COUNT) {
        fprintf(stderr, "Usage: jelli_sound_export CUE[0..6] OUTPUT.wav\n");
        return 2;
    }
    FILE *file = fopen(argv[2], "wb");
    if (!file)
        return 1;
    bool ok = export_wav(file, (unsigned)(argv[1][0] - '0'));
    if (fclose(file) != 0)
        ok = false;
    return ok ? 0 : 1;
}

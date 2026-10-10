#include "jelli/save.h"
#include "jelli/sound.h"
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

static void repair(uint8_t *bytes, size_t size)
{
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (i * 8u));
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0u; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    crc = ~crc;
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(crc >> (i * 8u));
}

static void saved_volume(void)
{
    JelliSave save = {0}, loaded;
    jelli_game_init(&save.game);
    CHECK(save.game.volume == 65u);
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    for (unsigned volume = 0u; volume <= 100u; volume += 5u) {
        save.game.volume = (uint8_t)volume;
        size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
        CHECK(size && jelli_save_decode(&loaded, bytes, size));
        CHECK(loaded.game.volume == volume);
    }
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    bytes[size - 9u - 4u * (size_t)save.game.count] = 101u;
    repair(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size));
    /* A v6 save has no volume byte; its exact legacy payload remains valid. */
    size_t extension = 1u + 4u * (size_t)save.game.count;
    memmove(bytes + size - 8u - extension, bytes + size - 8u, 8u);
    size -= extension;
    bytes[4] = 6u;
    repair(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.volume == JELLI_VOLUME_DEFAULT);
}

static void volume_commands(void)
{
    JelliGame game, scratch;
    jelli_game_init(&game);
    CHECK(jelli_game_check(&game, (JelliCommand){JELLI_CMD_VOLUME, 1u, 75u}, &scratch) == JELLI_OK);
    CHECK(game.volume == 65u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_VOLUME, 1u, 101u}) ==
          JELLI_INVALID_TARGET);
    CHECK(game.volume == 65u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_VOLUME, 1u, 0u}) == JELLI_OK);
    CHECK(game.volume == 0u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_VOLUME, 1u, 0u}) == JELLI_FULL);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, 1u, 0u}) == JELLI_OK);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_VOLUME, 1u, 100u}) == JELLI_OK);
    CHECK(game.volume == 100u && game.pets[0].asleep);
    CHECK(jelli_sound_volume(5u, 50u) == 25u && jelli_sound_volume(6u, 50u) == 18u);
    CHECK(jelli_sound_volume(5u, 65u) == 33u && jelli_sound_volume(6u, 65u) == 23u);
    for (unsigned cue = 0u; cue < JELLI_SOUND_COUNT; ++cue) {
        CHECK(jelli_sound_volume(cue, 0u) == 0u);
        CHECK(jelli_sound_volume(cue, 100u) <= 80u);
    }
    CHECK(jelli_sound_volume(5u, UINT32_MAX) == 0u);
}

int main(void)
{
    saved_volume();
    volume_commands();
    puts("PASS: volume migration, persistence, bounds, preflight and gain mapping");
    return 0;
}

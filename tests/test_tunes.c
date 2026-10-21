#include "game_fixture.h"
#include "jelli/save.h"
#include "jelli/music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #e);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static JelliSave saved, loaded;
static uint8_t bytes[JELLI_SAVE_CAPACITY];

static void repair(size_t size)
{
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (i * 8u));
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(~crc >> (i * 8u));
}

int main(void)
{
    test_game_pair(&saved.game);
    CHECK(!saved.game.tunes_seen);
    saved.game.tunes_seen = (uint16_t)JELLI_MUSIC_SEEN_MASK;
    saved.game.activity_music_enabled = false;
    saved.game.cartridge_stats[1].plays = 2u;
    size_t size = jelli_save_encode(&saved, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.tunes_seen == JELLI_MUSIC_SEEN_MASK);
    CHECK(!loaded.game.activity_music_enabled && loaded.game.cartridge_stats[1].plays == 2u);
    CHECK(!loaded.game.prizes.owned && !loaded.game.pets[0].collectible_progress.record_rewarded);
    loaded.sequence = 321u;
    bytes[size - 10u] |= 1u; /* A rare record cannot be unlocked by a discovery bit. */
    repair(size);
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == 321u);
    size = jelli_save_encode(&saved, bytes, sizeof(bytes));
    bytes[size - 9u] |= 0x80u; /* Future/unsupported song bit. */
    repair(size);
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == 321u);
    size = jelli_save_encode(&saved, bytes, sizeof(bytes));
    memmove(bytes + size - 10u, bytes + size - 8u, 8u);
    size -= 2u;
    bytes[4] = 18u;
    repair(size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(!loaded.game.tunes_seen && !loaded.game.activity_music_enabled);
    CHECK(loaded.game.cartridge_stats[1].plays == 2u);
    saved.game.tunes_seen |= 1u;
    CHECK(!jelli_game_valid(&saved.game) && !jelli_save_encode(&saved, bytes, sizeof(bytes)));
    CHECK(!strcmp(jelli_music_name(JELLI_MUSIC_LAUNCH), "Pocket Arcade"));
    CHECK(!strcmp(jelli_music_name(JELLI_MUSIC_WORKOUT), "Iron Pulse"));
    CHECK(!*jelli_music_name(JELLI_MUSIC_TRACK_COUNT));
    puts("PASS: tune discovery encoding, legacy migration, unsupported bits and stable catalog");
    return 0;
}

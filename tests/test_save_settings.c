#include "jelli/save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* Version 4 appends one settings byte (battery display) after the offered prize,
 * and version 6 the sound level after it, just before the 8-byte trailer. Version 5
 * kept the version 4 layout; earlier versions end the game record at the offered prize. */
enum { TRAILER = 8u, SOUND_FROM_END = TRAILER + 1u, SETTING_FROM_END = TRAILER + 2u };

static void repair_checksum(uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < size - TRAILER; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? UINT32_C(0xedb88320) : 0u);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4u; ++i)
        bytes[size - TRAILER + i] = (uint8_t)(crc >> (8u * i));
}

static JelliSave fixture(void)
{
    JelliSave save = {.sequence = 41u, .anchor_ms = 1700000000000u, .anchor_valid = true};
    jelli_game_init(&save.game);
    save.game.timezone_minutes = -330;
    save.game.clock_adjust = 25;
    save.game.prizes.owned = 1u;
    save.game.prizes.discovered = 3u;
    save.game.prizes.origin_pet[0] = 1u;
    save.game.prizes.origin_pet[1] = 2u;
    save.game.prizes.offered = 3u;
    save.game.prizes.offered_pet = 2u;
    return save;
}

static size_t encode(const JelliSave *save, uint8_t *bytes)
{
    size_t size = jelli_save_encode(save, bytes, JELLI_SAVE_CAPACITY);
    CHECK(size > SETTING_FROM_END);
    return size;
}

/* Rewrites a current save as an older version: the same layout minus the sound level
 * (versions 4 and 5), and minus the battery setting too (version 3). */
static size_t as_version(const uint8_t *current, size_t size, uint8_t *legacy, uint8_t version)
{
    size_t dropped = version < 4u ? 2u : 1u, old_size = size - dropped;
    memcpy(legacy, current, size - TRAILER - dropped);
    memcpy(legacy + old_size - TRAILER, current + size - TRAILER, TRAILER);
    legacy[4] = version;
    for (unsigned i = 0; i < 4u; ++i)
        legacy[8u + i] = (uint8_t)(old_size >> (i * 8u));
    repair_checksum(legacy, old_size);
    return old_size;
}

static void round_trip(void)
{
    JelliSave save = fixture(), loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY], again[JELLI_SAVE_CAPACITY];
    CHECK(save.game.battery_display); /* New games show the indicator. */
    for (unsigned shown = 0u; shown < 2u; ++shown) {
        save.game.battery_display = shown != 0u;
        size_t size = encode(&save, bytes);
        CHECK(bytes[4] == 6u && JELLI_SAVE_VERSION == 6u);
        CHECK(bytes[size - SETTING_FROM_END] == shown);
        CHECK(bytes[size - SOUND_FROM_END] == JELLI_SOUND_LEVEL_DEFAULT);
        CHECK(jelli_save_decode(&loaded, bytes, size));
        CHECK(loaded.game.battery_display == (shown != 0u));
        CHECK(loaded.game.timezone_minutes == -330 && loaded.game.prizes.offered == 3u);
        CHECK(encode(&loaded, again) == size && memcmp(bytes, again, size) == 0);
    }
}

static void version3_defaults_to_shown(void)
{
    JelliSave save = fixture(), loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY], legacy[JELLI_SAVE_CAPACITY];
    save.game.battery_display = false;
    save.game.sound_level = 0u;
    size_t size = encode(&save, bytes);
    size_t old_size = as_version(bytes, size, legacy, 3u);
    CHECK(jelli_save_decode(&loaded, legacy, old_size));
    CHECK(loaded.game.battery_display && loaded.sequence == save.sequence);
    CHECK(loaded.game.sound_level == JELLI_SOUND_LEVEL_DEFAULT);
    CHECK(loaded.game.timezone_minutes == -330 && loaded.game.clock_adjust == 25);
    CHECK(loaded.game.prizes.owned == 1u && loaded.game.prizes.offered_pet == 2u);
    /* Re-saving migrates to version 6 with both defaults made explicit. */
    size_t migrated = encode(&loaded, bytes);
    CHECK(migrated == size && bytes[4] == 6u && bytes[size - SETTING_FROM_END] == 1u);
    CHECK(bytes[size - SOUND_FROM_END] == JELLI_SOUND_LEVEL_DEFAULT);
}

/* Every level round-trips through version 6; versions 4 and 5 predate the setting
 * and load the middle level while keeping their stored battery setting. */
static void sound_level_versions(void)
{
    JelliSave save = fixture(), loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY], legacy[JELLI_SAVE_CAPACITY];
    CHECK(save.game.sound_level == 5u && JELLI_SOUND_LEVEL_DEFAULT == 5u);
    for (unsigned level = 0u; level <= JELLI_SOUND_LEVEL_MAX; ++level) {
        save.game.sound_level = (uint8_t)level;
        save.game.battery_display = (level & 1u) != 0u;
        size_t size = encode(&save, bytes);
        CHECK(bytes[size - SOUND_FROM_END] == level);
        CHECK(jelli_save_decode(&loaded, bytes, size) && loaded.game.sound_level == level);
        CHECK(loaded.game.battery_display == ((level & 1u) != 0u));
        for (uint8_t version = 4u; version <= 5u; ++version) {
            size_t old_size = as_version(bytes, size, legacy, version);
            CHECK(jelli_save_decode(&loaded, legacy, old_size));
            CHECK(loaded.game.sound_level == JELLI_SOUND_LEVEL_DEFAULT);
            CHECK(loaded.game.battery_display == ((level & 1u) != 0u));
        }
    }
}

/* Levels above the maximum are corrupt, whatever else the record holds. */
static void sound_level_out_of_range(void)
{
    JelliSave save = fixture(), loaded = fixture();
    loaded.sequence = 9u;
    loaded.game.sound_level = 2u;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    for (unsigned value = JELLI_SOUND_LEVEL_MAX + 1u; value < 256u; ++value) {
        size_t size = encode(&save, bytes);
        bytes[size - SOUND_FROM_END] = (uint8_t)value;
        repair_checksum(bytes, size);
        CHECK(!jelli_save_decode(&loaded, bytes, size));
        CHECK(loaded.sequence == 9u && loaded.game.sound_level == 2u); /* Output unchanged. */
    }
    JelliGame game;
    jelli_game_init(&game);
    game.sound_level = JELLI_SOUND_LEVEL_MAX;
    CHECK(jelli_game_valid(&game));
    game.sound_level = JELLI_SOUND_LEVEL_MAX + 1u;
    CHECK(!jelli_game_valid(&game)); /* The encoder refuses it as well. */
    save.game.sound_level = JELLI_SOUND_LEVEL_MAX + 1u;
    CHECK(jelli_save_encode(&save, bytes, sizeof(bytes)) == 0u);
}

static void malformed_setting_is_rejected(void)
{
    JelliSave save = fixture(), loaded = fixture(), before;
    loaded.sequence = 7u;
    before = loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = encode(&save, bytes);
    bytes[size - SETTING_FROM_END] = 2u; /* Strict boolean. */
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == before.sequence);
    size = encode(&save, bytes);
    bytes[4] = 3u; /* A version 3 header cannot carry the extra bytes. */
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == before.sequence);
    size = encode(&save, bytes);
    bytes[4] = 5u; /* Nor a version 5 header the sound level. */
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == before.sequence);
    size = encode(&save, bytes);
    bytes[4] = JELLI_SAVE_VERSION + 1u; /* Future versions are preserved, not decoded. */
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == before.sequence);
}

static void full_roster_fits(void)
{
    JelliSave save = fixture(), loaded;
    save.game.count = JELLI_PET_CAPACITY;
    for (unsigned p = 2u; p < save.game.count; ++p) {
        save.game.pets[p] = save.game.pets[0];
        save.game.pets[p].id = p + 1u;
    }
    save.game.battery_display = false;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = encode(&save, bytes);
    CHECK(size <= JELLI_SAVE_CAPACITY && jelli_save_decode(&loaded, bytes, size));
    CHECK(!loaded.game.battery_display);
    printf("eight-pet v6 save: %zu of %u bytes\n", size, (unsigned)JELLI_SAVE_CAPACITY);
}

int main(void)
{
    round_trip();
    version3_defaults_to_shown();
    sound_level_versions();
    sound_level_out_of_range();
    malformed_setting_is_rejected();
    full_roster_fits();
    puts("PASS: v4 battery and v6 sound level settings round trip, legacy defaults, strict "
         "decoding");
    return 0;
}

#include "jelli/game.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static const char *const base_path = ".jelli-storage-test";

static FILE *open_file(const char *path, const char *mode)
{
#ifdef _MSC_VER
    FILE *file = NULL;
    return fopen_s(&file, path, mode) == 0 ? file : NULL;
#else
    return fopen(path, mode);
#endif
}

static int make_directory(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0700);
#endif
}

static int remove_directory(const char *path)
{
#ifdef _WIN32
    return _rmdir(path);
#else
    return rmdir(path);
#endif
}

static void remove_slots(void)
{
    (void)remove(".jelli-storage-test.0");
    (void)remove(".jelli-storage-test.1");
    (void)remove_directory(".jelli-storage-test.0");
    (void)remove_directory(".jelli-storage-test.1");
}

static JelliSave fixture(uint64_t sequence)
{
    JelliSave save = {.sequence = sequence, .anchor_ms = sequence * 100u, .anchor_valid = true};
    jelli_game_init(&save.game);
    return save;
}

static JelliStorageResult load(JelliSave *save, uint8_t *work)
{
    return jelli_sdl_storage_load(base_path, save, work, JELLI_SAVE_CAPACITY);
}

static void write_save(const JelliSave *save, uint8_t *work)
{
    CHECK(jelli_sdl_storage_write(base_path, save, work, JELLI_SAVE_CAPACITY) == JELLI_STORAGE_OK);
}

static bool same_save(const JelliSave *left, const JelliSave *right)
{
    uint8_t left_bytes[JELLI_SAVE_CAPACITY];
    uint8_t right_bytes[JELLI_SAVE_CAPACITY];
    size_t left_size = jelli_save_encode(left, left_bytes, sizeof(left_bytes));
    size_t right_size = jelli_save_encode(right, right_bytes, sizeof(right_bytes));
    return left_size != 0u && left_size == right_size &&
           memcmp(left_bytes, right_bytes, left_size) == 0;
}

static void flip_last_byte(const char *path)
{
    FILE *file = open_file(path, "r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, -1L, SEEK_END) == 0);
    int value = fgetc(file);
    CHECK(value != EOF);
    CHECK(fseek(file, -1L, SEEK_CUR) == 0);
    CHECK(fputc(value ^ 1, file) != EOF);
    CHECK(fclose(file) == 0);
}

static void set_record_byte(const char *path, long offset, uint8_t value)
{
    FILE *file = open_file(path, "r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, offset, SEEK_SET) == 0);
    CHECK(fputc(value, file) != EOF);
    CHECK(fclose(file) == 0);
}

static void write_file(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file = open_file(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(bytes, 1u, size, file) == size);
    CHECK(fclose(file) == 0);
}

/* Rewrites a current record as an older version by dropping trailing settings bytes:
 * version 6 shares the current layout, versions 4 and 5 lack the version 6 sound
 * level, and version 3 also the battery setting. */
static size_t as_version(uint8_t *bytes, size_t size, uint8_t version)
{
    size_t old_size = size - (version >= 6u ? 0u : version < 4u ? 2u : 1u);
    memmove(bytes + old_size - 8u, bytes + size - 8u, 8u);
    bytes[4] = version;
    for (unsigned i = 0; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(old_size >> (8u * i));
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < old_size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? UINT32_C(0xedb88320) : 0u);
    }
    for (unsigned i = 0; i < 4u; ++i)
        bytes[old_size - 8u + i] = (uint8_t)(~crc >> (8u * i));
    return old_size;
}

/* Shared JELLI_SAVE_VERSION: v3 files still load (indicator shown), v4+ keeps the
 * stored setting, and versions outside 1..JELLI_SAVE_VERSION are preserved. */
static void battery_setting_versions(uint8_t *work)
{
    remove_slots();
    JelliSave hidden = fixture(5u), loaded = fixture(90u);
    hidden.game.battery_display = false;
    write_save(&hidden, work);
    CHECK(load(&loaded, work) == JELLI_STORAGE_OK && !loaded.game.battery_display);
    remove_slots();
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&hidden, bytes, sizeof(bytes));
    CHECK(size != 0u);
    size_t old_size = as_version(bytes, size, 3u);
    write_file(".jelli-storage-test.0", bytes, old_size);
    CHECK(load(&loaded, work) == JELLI_STORAGE_OK && loaded.sequence == 5u);
    CHECK(loaded.game.battery_display);
    static const uint8_t versions[] = {0u, JELLI_SAVE_VERSION + 1u, 255u};
    for (unsigned i = 0; i < sizeof(versions); ++i) {
        set_record_byte(".jelli-storage-test.0", 4L, versions[i]);
        JelliSave before = loaded;
        CHECK(load(&loaded, work) == JELLI_STORAGE_INCOMPATIBLE && same_save(&loaded, &before));
        CHECK(jelli_sdl_storage_write(base_path, &hidden, work, JELLI_SAVE_CAPACITY) ==
              JELLI_STORAGE_INCOMPATIBLE);
    }
    remove_slots();
}

/* The current version keeps the stored sound level; the same game written as version 5
 * loads the default, and the next save writes the current version with that default. */
static void sound_setting_versions(uint8_t *work)
{
    remove_slots();
    JelliSave quiet = fixture(6u), loaded = fixture(90u);
    quiet.game.sound_level = 9u;
    write_save(&quiet, work);
    CHECK(load(&loaded, work) == JELLI_STORAGE_OK && loaded.game.sound_level == 9u);
    remove_slots();
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&quiet, bytes, sizeof(bytes));
    CHECK(size != 0u && bytes[4] == JELLI_SAVE_VERSION && bytes[size - 9u] == 9u);
    size_t old_size = as_version(bytes, size, 5u);
    write_file(".jelli-storage-test.0", bytes, old_size);
    CHECK(load(&loaded, work) == JELLI_STORAGE_OK && loaded.sequence == 6u);
    CHECK(loaded.game.sound_level == JELLI_SOUND_LEVEL_DEFAULT);
    loaded.sequence = 7u;
    write_save(&loaded, work);
    JelliSave again = fixture(90u);
    CHECK(load(&again, work) == JELLI_STORAGE_OK && again.sequence == 7u);
    CHECK(again.game.sound_level == JELLI_SOUND_LEVEL_DEFAULT);
    remove_slots();
}

/* A three-pet record (v5/v6, before the unicorn) through SDL storage: it loads with
 * the unicorn appended in slot 3 and the stored active pet unchanged, and the next
 * save and load keep exactly one unicorn. */
static void unicorn_migration_keeps_active(uint8_t *work)
{
    enum { PREFIX = 71u, COUNT_OFFSET = 68u };
    const size_t pet = 130u + JELLI_HABIT_BIN_COUNT * 6u + 37u + JELLI_PRIZE_COUNT * 2u + 9u;
    for (uint8_t version = 5u; version <= 6u; ++version)
        for (uint8_t active = 0u; active < 3u; ++active) {
            remove_slots();
            JelliSave stored = fixture(30u + active), loaded = fixture(90u);
            stored.game.active = active;
            uint8_t bytes[JELLI_SAVE_CAPACITY];
            size_t size = jelli_save_encode(&stored, bytes, sizeof(bytes));
            CHECK(size > PREFIX + 4u * pet && bytes[COUNT_OFFSET] == 4u);
            size_t cut = PREFIX + 3u * pet; /* Drop the unicorn's record. */
            memmove(bytes + cut, bytes + cut + pet, size - cut - pet);
            bytes[COUNT_OFFSET] = 3u;
            size = as_version(bytes, size - pet, version);
            write_file(".jelli-storage-test.0", bytes, size);
            CHECK(load(&loaded, work) == JELLI_STORAGE_OK && loaded.sequence == 30u + active);
            CHECK(loaded.game.count == 4u && loaded.game.active == active);
            CHECK(loaded.game.pets[3].form == JELLI_FORM_UNICORN &&
                  loaded.game.pets[3].id == JELLI_UNICORN_PET_ID);
            loaded.sequence = 50u;
            write_save(&loaded, work);
            JelliSave again = fixture(90u);
            CHECK(load(&again, work) == JELLI_STORAGE_OK && again.sequence == 50u);
            CHECK(again.game.count == 4u && again.game.active == active);
            CHECK(same_save(&again, &loaded));
        }
    remove_slots();
}

int main(void)
{
    uint8_t work[JELLI_SAVE_CAPACITY];
    JelliSave loaded = fixture(90u);
    remove_slots();
    CHECK(load(&loaded, work) == JELLI_STORAGE_NO_SAVE);
    JelliSave first = fixture(1u);
    write_save(&first, work);
    JelliSave second = fixture(2u);
    write_save(&second, work);
    CHECK(load(&loaded, work) == JELLI_STORAGE_OK && loaded.sequence == 2u);
    CHECK(jelli_sdl_storage_write(base_path, &second, work, sizeof(work)) == JELLI_STORAGE_INVALID);
    CHECK(load(&loaded, work) == JELLI_STORAGE_OK && loaded.sequence == 2u);
    flip_last_byte(".jelli-storage-test.1");
    CHECK(load(&loaded, work) == JELLI_STORAGE_OK && loaded.sequence == 1u);
    JelliSave third = fixture(3u);
    write_save(&third, work);
    CHECK(load(&loaded, work) == JELLI_STORAGE_OK && loaded.sequence == 3u);
    JelliSave before_io = loaded;
    CHECK(remove(".jelli-storage-test.1") == 0);
    CHECK(make_directory(".jelli-storage-test.1") == 0);
    CHECK(load(&loaded, work) == JELLI_STORAGE_IO_ERROR);
    CHECK(same_save(&loaded, &before_io));
    CHECK(remove_directory(".jelli-storage-test.1") == 0);
    set_record_byte(".jelli-storage-test.0", 29L, 2u);
    CHECK(load(&loaded, work) == JELLI_STORAGE_INCOMPATIBLE);
    JelliSave fourth = fixture(4u);
    CHECK(jelli_sdl_storage_write(base_path, &fourth, work, sizeof(work)) ==
          JELLI_STORAGE_INCOMPATIBLE);
    set_record_byte(".jelli-storage-test.0", 29L, 1u);
    set_record_byte(".jelli-storage-test.0", 4L, JELLI_SAVE_VERSION + 1u);
    CHECK(load(&loaded, work) == JELLI_STORAGE_INCOMPATIBLE);
    CHECK(jelli_sdl_storage_write(base_path, &fourth, work, sizeof(work)) ==
          JELLI_STORAGE_INCOMPATIBLE);
    CHECK(same_save(&loaded, &before_io));
    remove_slots();
    battery_setting_versions(work);
    sound_setting_versions(work);
    unicorn_migration_keeps_active(work);
    puts("PASS: SDL save slots recover newest valid snapshot and preserve incompatible data");
    return 0;
}

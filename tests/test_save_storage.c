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
    puts("PASS: SDL save slots recover newest valid snapshot and preserve incompatible data");
    return 0;
}

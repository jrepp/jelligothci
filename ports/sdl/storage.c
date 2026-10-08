#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L // NOLINT(bugprone-reserved-identifier): exposes POSIX fsync/fileno.
#endif

#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define STORAGE_PATH_CAPACITY 1024u

typedef enum { SLOT_MISSING, SLOT_VALID, SLOT_CORRUPT, SLOT_INCOMPATIBLE, SLOT_IO } SlotKind;

typedef struct {
    SlotKind kind;
    uint64_t sequence;
} Slot;

static FILE *open_file(const char *path, const char *mode)
{
#ifdef _MSC_VER
    FILE *file = NULL;
    return fopen_s(&file, path, mode) == 0 ? file : NULL;
#else
    return fopen(path, mode);
#endif
}

static bool make_path(char *path, size_t capacity, const char *base, unsigned slot)
{
    int written = snprintf(path, capacity, "%s.%u", base, slot);
    return written >= 0 && (size_t)written < capacity;
}

static bool is_incompatible(const uint8_t *bytes, size_t size)
{
    bool save_record = size >= 6u && bytes[0] == (uint8_t)'J' && bytes[1] == (uint8_t)'L' &&
                       bytes[2] == (uint8_t)'S' && bytes[3] == (uint8_t)'V';
    bool save_version_mismatch = save_record && (bytes[4] != 1u || bytes[5] != 0u);
    bool content_version_mismatch =
        save_record && size >= 30u && bytes[4] == 1u && bytes[5] == 0u && bytes[29] != 1u;
    return save_version_mismatch || content_version_mismatch;
}

static Slot read_slot(const char *path, uint8_t *work, size_t capacity)
{
    Slot slot;
    memset(&slot, 0, sizeof(slot));
    FILE *file = open_file(path, "rb");
    if (file == NULL) {
        slot.kind = errno == ENOENT ? SLOT_MISSING : SLOT_IO;
        return slot;
    }
    size_t size = fread(work, 1u, capacity, file);
    bool too_large = size == capacity && fgetc(file) != EOF;
    bool read_error = ferror(file) != 0;
    bool close_error = fclose(file) != 0;
    if (read_error || close_error) {
        slot.kind = SLOT_IO;
    } else if (too_large) {
        slot.kind = SLOT_CORRUPT;
    } else if (is_incompatible(work, size)) {
        slot.kind = SLOT_INCOMPATIBLE;
    } else {
        JelliSave decoded;
        if (jelli_save_decode(&decoded, work, size)) {
            slot.kind = SLOT_VALID;
            slot.sequence = decoded.sequence;
        } else {
            slot.kind = SLOT_CORRUPT;
        }
    }
    return slot;
}

static Slot read_selected(const char *path, uint8_t *work, size_t capacity, JelliSave *save)
{
    Slot slot = {.kind = SLOT_IO};
    FILE *file = open_file(path, "rb");
    if (file == NULL)
        return slot;
    size_t size = fread(work, 1u, capacity, file);
    bool too_large = size == capacity && fgetc(file) != EOF;
    bool read_error = ferror(file) != 0;
    bool close_error = fclose(file) != 0;
    if (read_error || close_error)
        return slot;
    slot.kind = SLOT_CORRUPT;
    if (too_large)
        return slot;
    if (is_incompatible(work, size)) {
        slot.kind = SLOT_INCOMPATIBLE;
        return slot;
    }
    if (jelli_save_decode(save, work, size)) {
        slot.kind = SLOT_VALID;
        slot.sequence = save->sequence;
    }
    return slot;
}

static JelliStorageResult load_selected(const char *path, uint64_t sequence, uint8_t *work,
                                        JelliSave *save)
{
    JelliSave decoded;
    Slot selected = read_selected(path, work, JELLI_SAVE_CAPACITY, &decoded);
    if (selected.kind == SLOT_IO)
        return JELLI_STORAGE_IO_ERROR;
    if (selected.kind == SLOT_INCOMPATIBLE)
        return JELLI_STORAGE_INCOMPATIBLE;
    if (selected.kind != SLOT_VALID || selected.sequence != sequence)
        return JELLI_STORAGE_CORRUPT;
    *save = decoded;
    return JELLI_STORAGE_OK;
}

static bool valid_work(const uint8_t *work, size_t capacity)
{
    return work != NULL && capacity >= JELLI_SAVE_CAPACITY;
}

static JelliStorageResult slots_error(Slot first, Slot second)
{
    if (first.kind == SLOT_IO || second.kind == SLOT_IO)
        return JELLI_STORAGE_IO_ERROR;
    if (first.kind == SLOT_INCOMPATIBLE || second.kind == SLOT_INCOMPATIBLE)
        return JELLI_STORAGE_INCOMPATIBLE;
    if (first.kind == SLOT_CORRUPT || second.kind == SLOT_CORRUPT)
        return JELLI_STORAGE_CORRUPT;
    return JELLI_STORAGE_NO_SAVE;
}

JelliStorageResult jelli_sdl_storage_load(const char *base_path, JelliSave *save, uint8_t *work,
                                          size_t work_capacity)
{
    if (base_path == NULL || base_path[0] == '\0' || save == NULL ||
        !valid_work(work, work_capacity))
        return JELLI_STORAGE_INVALID;
    char path0[STORAGE_PATH_CAPACITY];
    char path1[STORAGE_PATH_CAPACITY];
    if (!make_path(path0, sizeof(path0), base_path, 0u) ||
        !make_path(path1, sizeof(path1), base_path, 1u))
        return JELLI_STORAGE_INVALID;
    Slot first = read_slot(path0, work, JELLI_SAVE_CAPACITY);
    Slot second = read_slot(path1, work, JELLI_SAVE_CAPACITY);
    if (first.kind == SLOT_IO || second.kind == SLOT_IO)
        return JELLI_STORAGE_IO_ERROR;
    if (first.kind == SLOT_INCOMPATIBLE || second.kind == SLOT_INCOMPATIBLE)
        return JELLI_STORAGE_INCOMPATIBLE;
    if (first.kind == SLOT_VALID && second.kind == SLOT_VALID) {
        const char *selected = first.sequence > second.sequence ? path0 : path1;
        uint64_t expected = first.sequence > second.sequence ? first.sequence : second.sequence;
        return load_selected(selected, expected, work, save);
    }
    if (first.kind == SLOT_VALID)
        return load_selected(path0, first.sequence, work, save);
    if (second.kind == SLOT_VALID)
        return load_selected(path1, second.sequence, work, save);
    return slots_error(first, second);
}

static unsigned choose_target(Slot first, Slot second)
{
    if (first.kind == SLOT_MISSING || first.kind == SLOT_CORRUPT)
        return 0u;
    if (second.kind == SLOT_MISSING || second.kind == SLOT_CORRUPT)
        return 1u;
    if (first.sequence > second.sequence)
        return 1u;
    return 0u;
}

static int sync_file(FILE *file)
{
#ifdef _WIN32
    return _commit(_fileno(file));
#else
    return fsync(fileno(file));
#endif
}

static bool write_file(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file = open_file(path, "wb");
    if (file == NULL)
        return false;
    bool ok = fwrite(bytes, 1u, size, file) == size && fflush(file) == 0 && sync_file(file) == 0;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

static JelliStorageResult validate_write_slots(Slot first, Slot second, uint64_t sequence)
{
    if (first.kind == SLOT_IO || second.kind == SLOT_IO)
        return JELLI_STORAGE_IO_ERROR;
    if (first.kind == SLOT_INCOMPATIBLE || second.kind == SLOT_INCOMPATIBLE)
        return JELLI_STORAGE_INCOMPATIBLE;
    if (first.kind == SLOT_CORRUPT && second.kind != SLOT_VALID)
        return JELLI_STORAGE_CORRUPT;
    if (second.kind == SLOT_CORRUPT && first.kind != SLOT_VALID)
        return JELLI_STORAGE_CORRUPT;
    uint64_t newest = 0u;
    if (first.kind == SLOT_VALID)
        newest = first.sequence;
    if (second.kind == SLOT_VALID && second.sequence > newest)
        newest = second.sequence;
    if ((first.kind == SLOT_VALID || second.kind == SLOT_VALID) && sequence <= newest)
        return JELLI_STORAGE_INVALID;
    return JELLI_STORAGE_OK;
}

JelliStorageResult jelli_sdl_storage_write(const char *base_path, const JelliSave *save,
                                           uint8_t *work, size_t work_capacity)
{
    if (base_path == NULL || base_path[0] == '\0' || save == NULL ||
        !valid_work(work, work_capacity) || save->game.resuming)
        return JELLI_STORAGE_INVALID;
    char path0[STORAGE_PATH_CAPACITY];
    char path1[STORAGE_PATH_CAPACITY];
    if (!make_path(path0, sizeof(path0), base_path, 0u) ||
        !make_path(path1, sizeof(path1), base_path, 1u))
        return JELLI_STORAGE_INVALID;
    Slot first = read_slot(path0, work, JELLI_SAVE_CAPACITY);
    Slot second = read_slot(path1, work, JELLI_SAVE_CAPACITY);
    JelliStorageResult validation = validate_write_slots(first, second, save->sequence);
    if (validation != JELLI_STORAGE_OK)
        return validation;
    size_t encoded_size = jelli_save_encode(save, work, JELLI_SAVE_CAPACITY);
    if (encoded_size == 0u)
        return JELLI_STORAGE_INVALID;
    unsigned target = choose_target(first, second);
    const char *path = target == 0u ? path0 : path1;
    if (!write_file(path, work, encoded_size))
        return JELLI_STORAGE_IO_ERROR;
    Slot verified = read_slot(path, work, JELLI_SAVE_CAPACITY);
    if (verified.kind != SLOT_VALID || verified.sequence != save->sequence)
        return verified.kind == SLOT_IO ? JELLI_STORAGE_IO_ERROR : JELLI_STORAGE_CORRUPT;
    return JELLI_STORAGE_OK;
}

#ifndef JELLI_SDL_STORAGE_H
#define JELLI_SDL_STORAGE_H

#include "jelli/save.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    JELLI_STORAGE_OK,
    JELLI_STORAGE_NO_SAVE,
    JELLI_STORAGE_CORRUPT,
    JELLI_STORAGE_INCOMPATIBLE,
    JELLI_STORAGE_IO_ERROR,
    JELLI_STORAGE_INVALID
} JelliStorageResult;

/* base_path names two sibling files formed by appending .0 and .1. */
JelliStorageResult jelli_sdl_storage_load(const char *base_path, JelliSave *save, uint8_t *work,
                                          size_t work_capacity);
/* Caller supplies a JELLI_SAVE_CAPACITY byte work buffer; never call in a frame callback. */
JelliStorageResult jelli_sdl_storage_write(const char *base_path, const JelliSave *save,
                                           uint8_t *work, size_t work_capacity);

#endif

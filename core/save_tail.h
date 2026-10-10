#ifndef JELLI_SAVE_TAIL_H
#define JELLI_SAVE_TAIL_H
#include "jelli/game.h"
#include "save_codec.h"

/* Per-pet fields appended in save versions 9 and later, written after every older block. */
void jelli_save_write_tail(Writer *writer, const JelliGame *game);
void jelli_save_read_tail(Reader *reader, JelliGame *game);
#endif

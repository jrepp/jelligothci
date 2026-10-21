#ifndef JELLI_COLLECTIBLE_SAVE_H
#define JELLI_COLLECTIBLE_SAVE_H
#include "jelli/game.h"
#include "save_codec.h"
void jelli_collectible_save_write(Writer *writer, const JelliGame *game);
void jelli_collectible_save_read(Reader *reader, JelliGame *game);
#endif

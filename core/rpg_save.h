#ifndef JELLI_RPG_SAVE_H
#define JELLI_RPG_SAVE_H
#include "save_codec.h"
#include "jelli/game.h"
void jelli_rpg_save_write(Writer *writer, const JelliGame *game);
void jelli_rpg_save_read(Reader *reader, JelliGame *game);
#endif

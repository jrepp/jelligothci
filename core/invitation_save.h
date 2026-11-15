#ifndef JELLI_INVITATION_SAVE_H
#define JELLI_INVITATION_SAVE_H
#include "save_codec.h"
#include "jelli/game.h"
void jelli_invitation_save_write(Writer *writer, const JelliGame *game);
void jelli_invitation_save_read(Reader *reader, JelliGame *game);
#endif

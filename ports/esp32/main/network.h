#ifndef JELLI_NETWORK_H
#define JELLI_NETWORK_H
#include "jelli/debug.h"
#include "session.h"

void jelli_network_init(const JelliPetEngine *engine);
void jelli_network_poll(JelliPetEngine *engine, JelliEspSession *session, bool frozen);
bool jelli_network_command(void *ctx, JelliDebug *debug, const JelliPetEngine *engine, uint32_t id,
                           char **words, unsigned count);
#endif

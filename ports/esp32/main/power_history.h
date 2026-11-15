#ifndef JELLI_POWER_HISTORY_H
#define JELLI_POWER_HISTORY_H
#include "jelli/debug.h"
#include "jelli/power_log.h"

/* Init and append are owned by the battery worker; queries copy under a short
 * critical section and never perform NVS IO on the engine thread. */
void jelli_power_history_init(void);
void jelli_power_history_append(JelliPowerRecord record);
bool jelli_power_history_command(JelliDebug *debug, uint32_t id, char **words, unsigned count);
#endif

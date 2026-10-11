#ifndef JELLI_PANEL_TRIAL_H
#define JELLI_PANEL_TRIAL_H
#include "display_output.h"

bool jelli_panel_trial_deep_standby(void);
bool jelli_panel_trial_busy(void);
bool jelli_panel_trial_command(JelliDebug *debug, uint32_t id, char **words, unsigned count);
void jelli_panel_trial_poll(JelliDisplayOutput *output, JelliPetEngine *engine);
#endif

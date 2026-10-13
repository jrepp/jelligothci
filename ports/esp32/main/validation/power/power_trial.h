#ifndef JELLI_POWER_TRIAL_H
#define JELLI_POWER_TRIAL_H
#include "display_output.h"
/* Engine task only; exclusive diagnostic, bounded callback, no display lock held. */
esp_err_t jelli_power_sleep_trial(JelliDisplayOutput *output, JelliPetEngine *engine,
                                  esp_err_t (*hold)(void), esp_err_t *restore);
#endif

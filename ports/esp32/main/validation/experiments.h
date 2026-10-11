#ifndef JELLI_EXPERIMENTS_H
#define JELLI_EXPERIMENTS_H
#include "sdkconfig.h"
#include "display_output.h"

/* Host-only seam: init after BSP, poll on engine task, filter under LVGL lock.
 * Disabled builds have no probe storage, callbacks, or peripheral activity. */
#ifdef CONFIG_JELLI_POWER_EXPERIMENTS
void jelli_experiments_init(void);
void jelli_experiments_poll(JelliDisplayOutput *output, JelliPetEngine *engine);
bool jelli_experiments_command(JelliDebug *debug, uint32_t id, char **words, unsigned count);
bool jelli_experiments_filter_touch(lv_event_code_t code);
void jelli_experiments_network_wake(void);
uint32_t jelli_experiments_network_wakes(void);
#else
static inline void jelli_experiments_init(void) {}
static inline void jelli_experiments_poll(JelliDisplayOutput *output, JelliPetEngine *engine)
{
    (void)output;
    (void)engine;
}
static inline bool jelli_experiments_filter_touch(lv_event_code_t code)
{
    (void)code;
    return false;
}
static inline bool jelli_experiments_command(JelliDebug *debug, uint32_t id, char **words,
                                             unsigned count)
{
    (void)debug;
    (void)id;
    (void)words;
    (void)count;
    return false;
}
static inline void jelli_experiments_network_wake(void) {}
#endif
#endif

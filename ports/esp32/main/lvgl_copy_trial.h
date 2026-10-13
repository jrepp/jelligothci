#ifndef JELLI_LVGL_COPY_TRIAL_H
#define JELLI_LVGL_COPY_TRIAL_H
#include <stdbool.h>
/* Power-only; caller holds the display mutex and no profile is active. */
bool jelli_lvgl_copy_qualify(void);
void jelli_lvgl_copy_mode(bool libc_copy);
#endif

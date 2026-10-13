#ifndef JELLI_PIPELINE_PROFILE_H
#define JELLI_PIPELINE_PROFILE_H
#include "sdkconfig.h"
#include "jelli/debug.h"
#include "lvgl.h"
#include <stdint.h>

typedef enum {
    JELLI_PROFILE_ENGINE,
    JELLI_PROFILE_LOCK,
    JELLI_PROFILE_COPY,
    JELLI_PROFILE_CHECK,
    JELLI_PROFILE_SWAP,
    JELLI_PROFILE_STAGE,
    JELLI_PROFILE_TRANSFER,
    JELLI_PROFILE_RENDER,
    JELLI_PROFILE_LVGL,
    JELLI_PROFILE_LVGL_WAIT,
    JELLI_PROFILE_LVGL_FLUSH,
    JELLI_PROFILE_LVGL_WORK,
    JELLI_PROFILE_LVGL_IMAGE,
    JELLI_PROFILE_LVGL_FILL,
    JELLI_PROFILE_LVGL_TASKS,
    JELLI_PROFILE_LVGL_DISPATCH,
    JELLI_PROFILE_LVGL_DRAW_WAIT,
    JELLI_PROFILE_LVGL_EVENTS,
    JELLI_PROFILE_COUNT
} JelliProfileStage;
#ifdef CONFIG_JELLI_POWER_EXPERIMENTS
bool jelli_profile_active(void);
bool jelli_profile_overlay_mode(bool skipped);
void jelli_profile_attach(lv_display_t *display);
uint64_t jelli_profile_now(void);
void jelli_profile_record(JelliProfileStage stage, uint64_t start, uint32_t bytes);
void jelli_profile_submit(void);
void jelli_profile_submit_failed(void);
bool jelli_profile_command(JelliDebug *debug, uint32_t id, char **words, unsigned count);
#else
static inline void jelli_profile_attach(lv_display_t *display) { (void)display; }
static inline uint64_t jelli_profile_now(void) { return 0; }
static inline void jelli_profile_record(JelliProfileStage stage, uint64_t start, uint32_t bytes)
{
    (void)stage;
    (void)start;
    (void)bytes;
}
static inline void jelli_profile_submit(void) {}
static inline void jelli_profile_submit_failed(void) {}
static inline bool jelli_profile_command(JelliDebug *debug, uint32_t id, char **words,
                                         unsigned count)
{
    (void)debug;
    (void)id;
    (void)words;
    (void)count;
    return false;
}
#endif
#endif

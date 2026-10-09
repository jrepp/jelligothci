#ifndef JELLI_PET_ENGINE_H
#define JELLI_PET_ENGINE_H

#include "jelli/pet_ui.h"
#include "jelli/pet_screen.h"

typedef struct {
    JelliPlatform platform;
    JelliSurface surface;
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    JelliPetScreen screen;
    uint64_t last_ms;
    uint64_t animation_ms;
    bool running, paused;
} JelliPetEngine;

/* Separate mode preserves the shapes diagnostic and its public contract. */
bool jelli_pet_init(JelliPetEngine *engine, JelliPlatform platform, JelliSurface surface);
/* Screen sleep: after JELLI_SCREEN_SLEEP_MS without a polled TAP, SWIPE, or
 * TOGGLE_PAUSE, the frame keeps advancing the game exactly as before but skips
 * rendering and present entirely. While the screen sleeps, polled taps, swipes,
 * and pause toggles never reach the UI; only a double tap (two taps within
 * JELLI_SCREEN_DOUBLE_TAP_MS and JELLI_SCREEN_DOUBLE_TAP_PX) wakes it, and the
 * next frame is a full redraw of the unchanged UI. QUIT always quits. */
bool jelli_pet_frame(JelliPetEngine *engine);
/* True while the display should show frames. */
bool jelli_pet_screen_awake(const JelliPetEngine *engine);
/* Edge-triggered host notification: returns true once per state change since
 * the previous call and writes the new state to *awake. Call after each frame;
 * set display power on a true result only. A sleep and wake between two calls
 * coalesce into no change. */
bool jelli_pet_screen_changed(JelliPetEngine *engine, bool *awake);
/* Wakes the screen if asleep (forcing a full redraw next frame) and restarts the
 * idle timer. For trusted developer input such as the debug protocol. */
void jelli_pet_screen_wake(JelliPetEngine *engine, uint64_t now_ms);
/* Puts the screen to sleep now; a no-op while already asleep. */
void jelli_pet_screen_sleep(JelliPetEngine *engine, uint64_t now_ms);
/* The frame's pet sound cue ID + 1, or zero for silence. Call once per unfrozen
 * frame after jelli_pet_frame. Silent while paused (nothing is consumed) and while
 * the screen sleeps (cues are consumed and discarded, so none burst on wake). */
unsigned jelli_pet_sound_cue(JelliPetEngine *engine, uint64_t now_ms);

#endif

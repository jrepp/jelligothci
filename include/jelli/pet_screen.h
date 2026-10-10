#ifndef JELLI_PET_SCREEN_H
#define JELLI_PET_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

/* Screen sleep turns the display off; it is unrelated to the pet's own sleep
 * (JelliPet.asleep). The game keeps running while the screen is dark. */

/* Idle time without polled input before the screen goes dark (the pet's; another
 * app can choose its own with jelli_pet_screen_init_idle). */
#define JELLI_SCREEN_SLEEP_MS 180000u
/* A wake double tap: the second tap within this many ms of the first... */
#define JELLI_SCREEN_DOUBLE_TAP_MS 500u
/* ...and within this many surface pixels of it (Euclidean distance). */
#define JELLI_SCREEN_DOUBLE_TAP_PX 80

/* Fixed-size policy state; all times are injected monotonic milliseconds.
 * A clock that runs backwards re-anchors instead of underflowing, and no time
 * is ever added to another, so values near UINT64_MAX cannot wrap. */
typedef struct {
    uint64_t activity_ms; /* Last counted input while awake; the idle timer anchor. */
    uint64_t tap_ms;      /* Candidate first tap of a wake double tap. */
    uint64_t wake_ms;     /* When a double tap last woke the screen. */
    uint64_t idle_ms;     /* Idle time before sleep; zero means JELLI_SCREEN_SLEEP_MS. */
    int tap_x, tap_y;
    bool asleep, tap_pending, wake_guard;
    bool reported_asleep; /* Last state handed to the host by the change query. */
} JelliPetScreen;

/* Starts awake with the idle timer anchored at now_ms. */
void jelli_pet_screen_init(JelliPetScreen *screen, uint64_t now_ms);
/* The same, sleeping after idle_ms (zero for the default) instead. */
void jelli_pet_screen_init_idle(JelliPetScreen *screen, uint64_t now_ms, uint64_t idle_ms);
/* Counts user activity: restarts the idle timer (meaningful while awake). */
void jelli_pet_screen_activity(JelliPetScreen *screen, uint64_t now_ms);
/* Applies the idle rule; true exactly when this call puts the screen to sleep. */
bool jelli_pet_screen_idle(JelliPetScreen *screen, uint64_t now_ms);
/* Milliseconds until the idle rule fires; zero while asleep or already due. */
uint64_t jelli_pet_screen_remaining(const JelliPetScreen *screen, uint64_t now_ms);
/* Feeds a tap received while asleep. True when it completes a double tap, in
 * which case the screen is awake again and the idle timer restarted. Otherwise
 * the tap becomes the new candidate first tap. */
bool jelli_pet_screen_wake_tap(JelliPetScreen *screen, int x, int y, uint64_t now_ms);
/* Forgets a candidate first tap, e.g. after a swipe while asleep. */
void jelli_pet_screen_cancel_tap(JelliPetScreen *screen);
/* True for a tap that follows a double-tap wake within the double-tap window:
 * an extra tap of a triple tap must not press whatever lies beneath it. */
bool jelli_pet_screen_guarded(JelliPetScreen *screen, uint64_t now_ms);
/* Forces a state, e.g. for debug commands; waking restarts the idle timer. */
void jelli_pet_screen_set(JelliPetScreen *screen, bool asleep, uint64_t now_ms);

#endif

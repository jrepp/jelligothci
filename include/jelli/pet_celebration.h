#ifndef JELLI_PET_CELEBRATION_H
#define JELLI_PET_CELEBRATION_H
#include "jelli/pet_ui.h"

/* One pending presentation, latest eligible event wins. No saved state or heap.
 * Wait at most 30 animation-clock seconds for home/active playback to finish.
 * Switching pet/form, backwards time, or resume discards pending work. */
typedef struct {
    uint64_t queued_ms, last_ms;
    uint32_t cursor, pet_id;
    uint8_t form, pending;
} JelliPetCelebration;

/* Bindings are one-based definition indices, zero disabled. Events are consumed
 * once, including while disabled. suppress discards offline/resume events.
 * Paused calls consume events but defer starting playback. */
void jelli_pet_celebrations(JelliPetCelebration *state, JelliPetUi *ui, const JelliGame *game,
                            const JelliEventLog *events,
                            const uint8_t bindings[JELLI_EFFECT_TRIGGER_COUNT],
                            uint64_t animation_ms, bool suppress, bool paused);
#endif

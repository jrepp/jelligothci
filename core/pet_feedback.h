#ifndef JELLI_PET_FEEDBACK_H
#define JELLI_PET_FEEDBACK_H
#include "jelli/pet_ui.h"
void jelli_pet_feedback(JelliPetUi *ui, const JelliGame *game, JelliEventSnapshot before,
                        uint32_t actor_id, bool back, int x, int y);
/* Compare only a live frame's session transition; restored checkpoints stay silent. */
void jelli_pet_chain_feedback(JelliPetUi *ui, const JelliCollectibleSession *before,
                              const JelliCollectibleSession *after);
#endif

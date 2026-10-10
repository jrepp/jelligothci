#ifndef JELLI_PET_FEEDBACK_H
#define JELLI_PET_FEEDBACK_H
#include "jelli/pet_ui.h"
void jelli_pet_feedback(JelliPetUi *ui, const JelliGame *game, JelliEventSnapshot before,
                        uint32_t actor_id, bool back, int x, int y);
#endif

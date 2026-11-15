#ifndef JELLI_INVITATIONS_H
#define JELLI_INVITATIONS_H
#include "jelli/game.h"
#define JELLI_INVITATION_INTERVAL_MINUTES 30u
/* Fixed ledger per pet: timed offers plus one independently expiring leisure offer.
 * Bits use stable activity IDs. Retired/accepted offers remain announced for the day. */
bool jelli_invitations_same(const JelliInvitations *a, const JelliInvitations *b);
bool jelli_invitations_valid(const JelliInvitations *offers);
bool jelli_invitations_update(JelliGame *game);
bool jelli_invitation_grants(const JelliGame *game, const JelliPet *pet, unsigned id);
void jelli_invitation_consume(JelliPet *pet, unsigned id);
bool jelli_invitation_seen(JelliPet *pet, unsigned id);
bool jelli_moment_identity_allowed(const JelliGame *game, const JelliPet *pet, unsigned id);
uint64_t jelli_invitation_clock(const JelliGame *game, const JelliPet *pet, uint8_t *basis);
#endif

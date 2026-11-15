#include "invitation_save.h"
#include "jelli/invitations.h"

void jelli_invitation_save_write(Writer *w, const JelliGame *game)
{
    for (unsigned i = 0; i < game->count; ++i) {
        const JelliInvitations *o = &game->pets[i].invitations;
        put_u64(w, o->clock_high);
        put_u64(w, o->next_generation);
        put_u64(w, o->leisure_until);
        put_u32(w, o->pending);
        put_u32(w, o->seen);
        put_u32(w, o->announced);
        put_u8(w, o->leisure);
        put_u8(w, o->clock_basis);
    }
}

void jelli_invitation_save_read(Reader *r, JelliGame *game)
{
    if (r->version < 21u)
        return;
    for (unsigned i = 0; i < game->count; ++i) {
        JelliInvitations *o = &game->pets[i].invitations;
        o->clock_high = get_u64(r);
        o->next_generation = get_u64(r);
        o->leisure_until = get_u64(r);
        o->pending = get_u32(r);
        o->seen = get_u32(r);
        o->announced = get_u32(r);
        o->leisure = get_u8(r);
        o->clock_basis = get_u8(r);
        if (!jelli_invitations_valid(o))
            r->failed = true;
    }
}

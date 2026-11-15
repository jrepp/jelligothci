#include "jelli/invitations.h"
#include "jelli/activities.h"

bool jelli_invitations_same(const JelliInvitations *a, const JelliInvitations *b)
{
    return a->clock_high == b->clock_high && a->next_generation == b->next_generation &&
           a->leisure_until == b->leisure_until && a->pending == b->pending && a->seen == b->seen &&
           a->announced == b->announced && a->leisure == b->leisure &&
           a->clock_basis == b->clock_basis;
}

uint64_t jelli_invitation_clock(const JelliGame *game, const JelliPet *pet, uint8_t *basis)
{
    *basis = game->wall_known ? 2u : game->clock_known ? 3u : 1u;
    uint64_t minute = game->wall_known ? game->wall_seconds / 60u : pet->ticks / 600u;
    if (!game->wall_known && game->clock_known)
        return pet->ticks / JELLI_DAY_TICKS * 1440u + game->clock_minute;
    int offset = game->timezone_minutes + game->clock_adjust;
    if (!game->wall_known)
        minute += pet->phase_offset / 600u;
    if (offset < 0)
        return minute >= (unsigned)-offset ? minute - (unsigned)-offset : 0u;
    return UINT64_MAX - minute >= (unsigned)offset ? minute + (unsigned)offset : UINT64_MAX;
}

bool jelli_invitations_valid(const JelliInvitations *o)
{
    uint32_t mask =
        jelli_moment_count == 32u ? UINT32_MAX : (UINT32_C(1) << jelli_moment_count) - 1u;
    return o->clock_basis <= 3u && o->leisure <= jelli_moment_count && !(o->pending & ~mask) &&
           !(o->announced & ~mask) && !(o->seen & ~o->pending) && !(o->pending & ~o->announced) &&
           (!o->leisure || (o->pending & (UINT32_C(1) << (o->leisure - 1u)))) &&
           ((o->leisure != 0u) == (o->leisure_until != 0u));
}

bool jelli_moment_identity_allowed(const JelliGame *game, const JelliPet *pet, unsigned id)
{
    if (id >= jelli_moment_count)
        return false;
    const JelliMoment *m = &jelli_moments[id];
    return (!m->forms || (pet->form < 32u && (m->forms & (UINT32_C(1) << pet->form)))) &&
           (!m->requires || (pet->activity_day == jelli_activity_day(game, pet) &&
                             (pet->completed_moments & (UINT32_C(1) << (m->requires - 1u)))));
}

bool jelli_invitation_grants(const JelliGame *game, const JelliPet *pet, unsigned id)
{
    const JelliInvitations *o = &pet->invitations;
    if (id >= jelli_moment_count || o->leisure != id + 1u || !(o->pending & (UINT32_C(1) << id)) ||
        !jelli_moment_identity_allowed(game, pet, id))
        return false;
    uint8_t basis;
    uint64_t now = jelli_invitation_clock(game, pet, &basis);
    const JelliMoment *m = &jelli_moments[id];
    unsigned minute = (unsigned)(now % 1440u);
    return basis == o->clock_basis && now >= o->clock_high && now < o->leisure_until &&
           m->invitation_weight &&
           (m->invitation_relax_window || ((game->wall_known || game->clock_known) &&
                                           minute >= m->start_minute && minute < m->end_minute));
}

void jelli_invitation_consume(JelliPet *pet, unsigned id)
{
    if (id >= jelli_moment_count)
        return;
    JelliInvitations *o = &pet->invitations;
    o->pending &= ~(UINT32_C(1) << id);
    o->seen &= o->pending;
    if (o->leisure == id + 1u) {
        o->leisure = 0u;
        o->leisure_until = 0u;
    }
}

bool jelli_invitation_seen(JelliPet *pet, unsigned id)
{
    if (id >= jelli_moment_count)
        return false;
    uint32_t bit = UINT32_C(1) << id;
    if (!(pet->invitations.pending & bit) || (pet->invitations.seen & bit))
        return false;
    pet->invitations.seen |= bit;
    return true;
}

static bool eligible(const JelliGame *game, const JelliPet *pet, unsigned id, uint64_t now)
{
    const JelliMoment *m = &jelli_moments[id];
    unsigned minute = (unsigned)(now % 1440u);
    return jelli_moment_identity_allowed(game, pet, id) &&
           !(pet->invitations.announced & (UINT32_C(1) << id)) &&
           !jelli_moment_free_play(game, pet, id) && !jelli_moment_cost_hint(pet, id)[0] &&
           (m->invitation_relax_window || ((game->wall_known || game->clock_known) &&
                                           minute >= m->start_minute && minute < m->end_minute));
}

static void generate(const JelliGame *game, JelliPet *pet, uint64_t now)
{
    JelliInvitations *o = &pet->invitations;
    if (o->leisure || now < o->next_generation || pet->asleep || pet->activity != JELLI_IDLE ||
        game->collectible.active || UINT64_MAX - now < 180u)
        return;
    o->next_generation = now + JELLI_INVITATION_INTERVAL_MINUTES;
    unsigned total = 0u;
    for (unsigned i = 0; i < jelli_moment_count; ++i)
        if (eligible(game, pet, i, now))
            total += jelli_moments[i].invitation_weight;
    if (!total)
        return;
    /* One deterministic selection per admitted interval, persisted before presentation. */
    uint32_t hash = pet->id ^ (uint32_t)now * UINT32_C(2654435761);
    hash ^= hash >> 16;
    unsigned pick = hash % total;
    for (unsigned i = 0; i < jelli_moment_count; ++i) {
        if (!eligible(game, pet, i, now))
            continue;
        unsigned weight = jelli_moments[i].invitation_weight;
        if (pick < weight) {
            o->leisure = (uint8_t)(i + 1u);
            o->leisure_until = now + jelli_moments[i].invitation_minutes;
            o->pending |= UINT32_C(1) << i;
            o->announced |= UINT32_C(1) << i;
            return;
        }
        pick -= weight;
    }
}

static void reconcile_entries(const JelliGame *game, JelliPet *pet, bool active, uint64_t now)
{
    JelliInvitations *o = &pet->invitations;
    unsigned minute = (unsigned)(now % 1440u);
    for (unsigned i = 0; i < jelli_moment_count; ++i) {
        const JelliMoment *m = &jelli_moments[i];
        bool leisure = o->leisure == i + 1u;
        bool valid = leisure ? jelli_invitation_grants(game, pet, i)
                             : minute >= m->start_minute && minute < m->end_minute;
        if (!valid || !jelli_moment_identity_allowed(game, pet, i))
            jelli_invitation_consume(pet, i);
        bool special = m->start_minute || m->end_minute != 1440u || m->requires || m->forms;
        uint32_t bit = UINT32_C(1) << i;
        if (active && special && !m->invitation_weight && !m->random_weight &&
            (game->wall_known || game->clock_known) && !(o->announced & bit) &&
            !jelli_moment_free_play(game, pet, i) &&
            jelli_moment_available(game, pet, i) == JELLI_OK) {
            o->pending |= bit;
            o->announced |= bit;
        }
    }
}

static void reconcile(const JelliGame *game, JelliPet *pet, bool active)
{
    JelliInvitations *o = &pet->invitations;
    uint8_t basis;
    uint64_t now = jelli_invitation_clock(game, pet, &basis);
    if (o->clock_basis && o->clock_basis != basis) {
        /* A time-domain change invalidates deadlines, never grants an immediate reroll. */
        *o = (JelliInvitations){.next_generation =
                                    now <= UINT64_MAX - JELLI_INVITATION_INTERVAL_MINUTES
                                        ? now + JELLI_INVITATION_INTERVAL_MINUTES
                                        : UINT64_MAX};
    }
    o->clock_basis = basis;
    if (now < o->clock_high)
        return; /* Suspend presentation until this clock catches up; never extend expiry. */
    if (o->clock_high / 1440u != now / 1440u) {
        o->announced = o->leisure ? UINT32_C(1) << (o->leisure - 1u) : 0u;
        o->pending &= o->announced;
        o->seen &= o->pending;
    }
    o->clock_high = now;
    reconcile_entries(game, pet, active, now);
    if (active)
        generate(game, pet, now);
}

bool jelli_invitations_update(JelliGame *game)
{
    if (!game || game->resuming || !jelli_game_valid(game))
        return false;
    bool changed = false;
    for (unsigned i = 0; i < game->count; ++i) {
        JelliInvitations before = game->pets[i].invitations;
        reconcile(game, &game->pets[i], i == game->active);
        changed |= !jelli_invitations_same(&before, &game->pets[i].invitations);
    }
    if (changed)
        ++game->revision;
    return changed;
}

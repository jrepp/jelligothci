#include "game_internal.h"

#include <limits.h>

#define LIVE_TICK_MS UINT64_C(100)
#define LIVE_TICK_LIMIT 8u
#define LIVE_BACKLOG_LIMIT UINT64_C(2000)
#define RESUME_SEGMENT_MS UINT64_C(60000)
#define RESUME_SEGMENT_LIMIT 1u

static uint64_t saturating_add(uint64_t left, uint64_t right)
{
    return (UINT64_MAX - left < right) ? UINT64_MAX : left + right;
}

static uint64_t add_elapsed(JelliGame *game, uint64_t elapsed_ms)
{
    uint64_t room = LIVE_BACKLOG_LIMIT - game->backlog_ms;
    uint64_t admitted = (elapsed_ms > room) ? room : elapsed_ms;
    uint64_t dropped = elapsed_ms - admitted;
    game->backlog_ms += (uint32_t)admitted;
    game->discarded_ms = saturating_add(game->discarded_ms, dropped);
    return game->backlog_ms;
}

static void advance_ticks(JelliGame *game, uint64_t ticks, bool offline)
{
    JelliPet *pet = &game->pets[game->active];
    for (uint64_t i = 0u; i < ticks; ++i) {
        jelli_game_endpoint(game, pet, 1u, offline);
        game->ticks = saturating_add(game->ticks, 1u);
    }
}

static void advance_segment(JelliGame *game, uint64_t ticks)
{
    /* One bounded minute per call follows the same transitions as live ticks. */
    advance_ticks(game, ticks, true);
}

void jelli_game_advance(JelliGame *game, uint64_t elapsed_ms)
{
    if (!jelli_game_valid(game) || game->resuming)
        return;
    uint64_t available = add_elapsed(game, elapsed_ms);
    uint64_t ticks = available / LIVE_TICK_MS;
    if (ticks > LIVE_TICK_LIMIT)
        ticks = LIVE_TICK_LIMIT;
    uint64_t consumed = ticks * LIVE_TICK_MS;
    game->backlog_ms -= (uint32_t)consumed;
    advance_ticks(game, ticks, false);
}

void jelli_game_resume_begin(JelliGame *game, uint64_t elapsed_ms)
{
    if (!jelli_game_valid(game) || game->resuming)
        return;
    uint64_t admitted = elapsed_ms;
    if (admitted > JELLI_OFFLINE_CAP_MS) {
        game->discarded_ms = saturating_add(game->discarded_ms, admitted - JELLI_OFFLINE_CAP_MS);
        admitted = JELLI_OFFLINE_CAP_MS;
    }
    game->resume_remaining_ms = admitted;
    game->resuming = admitted != 0u;
}

static uint64_t process_resume_segment(JelliGame *game)
{
    uint64_t segment = game->resume_remaining_ms;
    if (segment > RESUME_SEGMENT_MS)
        segment = RESUME_SEGMENT_MS;
    uint64_t total = segment + game->backlog_ms;
    uint64_t ticks = total / LIVE_TICK_MS;
    game->backlog_ms = (uint32_t)(total % LIVE_TICK_MS);
    advance_segment(game, ticks);
    return segment;
}

bool jelli_game_resume_step(JelliGame *game)
{
    if (!jelli_game_valid(game))
        return false;
    if (!game->resuming)
        return true;
    for (uint8_t i = 0u; i < RESUME_SEGMENT_LIMIT && game->resume_remaining_ms > 0u; ++i) {
        uint64_t consumed = process_resume_segment(game);
        game->resume_remaining_ms -= consumed;
    }
    if (game->resume_remaining_ms == 0u)
        game->resuming = false;
    return !game->resuming;
}

#include "jelli/collectible.h"

bool jelli_collectible_progress_valid(const JelliCollectibleProgress *progress)
{
    if (!progress || (progress->special_unlocked & ~((1u << JELLI_COLLECTIBLE_CHAIN_PRIZE) |
                                                     (1u << JELLI_COLLECTIBLE_RECORD_PRIZE))))
        return false;
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i) {
        if (progress->play_ticks[i] > JELLI_COLLECTIBLE_PROGRESS_TICKS ||
            (i >= jelli_prize_count && progress->play_ticks[i]) ||
            ((progress->special_unlocked & (1u << i)) &&
             progress->play_ticks[i] != JELLI_COLLECTIBLE_PROGRESS_TICKS))
            return false;
    }
    if (progress->record_rewarded >= (1u << JELLI_COLLECTIBLE_RECORD_COUNT))
        return false;
    for (unsigned i = 0u; i < JELLI_COLLECTIBLE_RECORD_COUNT; ++i)
        if ((progress->record_rewarded & (1u << i)) &&
            progress->play_ticks[JELLI_COLLECTIBLE_RECORD_PRIZE + i] < 300u)
            return false;
    return true;
}

static bool session_empty(const JelliCollectibleSession *s)
{
    return !s->active && !s->paused && !s->generation && !s->elapsed_ticks && !s->record_cursor &&
           !s->mode && !s->prize && !s->chain.length;
}

static bool other_mode_valid(const JelliCollectibleSession *s)
{
    if (s->mode == JELLI_COLLECTIBLE_RECORD)
        return s->prize >= JELLI_COLLECTIBLE_RECORD_PRIZE &&
               s->prize < JELLI_COLLECTIBLE_RECORD_PRIZE + JELLI_COLLECTIBLE_RECORD_COUNT &&
               s->record_cursor <= JELLI_RECORD_SAMPLES &&
               s->elapsed_ticks == s->record_cursor / (JELLI_RECORD_RATE / 10u) &&
               (s->record_cursor != JELLI_RECORD_SAMPLES || s->paused);
    return s->mode == JELLI_COLLECTIBLE_GENERIC && s->prize != JELLI_COLLECTIBLE_BUBBLE_PRIZE &&
           s->prize != JELLI_COLLECTIBLE_CHAIN_PRIZE &&
           (s->prize < JELLI_COLLECTIBLE_RECORD_PRIZE ||
            s->prize >= JELLI_COLLECTIBLE_RECORD_PRIZE + JELLI_COLLECTIBLE_RECORD_COUNT) &&
           !s->record_cursor && s->elapsed_ticks <= 80u && (s->elapsed_ticks != 80u || s->paused);
}

static bool session_mode_valid(const JelliCollectibleSession *s)
{
    if (s->mode == JELLI_COLLECTIBLE_BUBBLE)
        return s->prize == JELLI_COLLECTIBLE_BUBBLE_PRIZE && !s->record_cursor &&
               jelli_bubble_valid(&s->bubble) &&
               (s->bubble.status == JELLI_BUBBLE_RUNNING || s->paused);
    if (s->mode == JELLI_COLLECTIBLE_CHAIN)
        return s->prize == JELLI_COLLECTIBLE_CHAIN_PRIZE && !s->record_cursor &&
               jelli_chain_valid(&s->chain) &&
               (s->chain.status == JELLI_CHAIN_RUNNING || s->paused);
    return other_mode_valid(s);
}

bool jelli_collectible_valid(const JelliGame *game)
{
    if (!game || game->count > JELLI_PET_CAPACITY || game->active >= game->count)
        return false;
    const JelliCollectibleSession *s = &game->collectible;
    if (!s->pet_id)
        return session_empty(s);
    if (!s->generation || s->prize >= jelli_prize_count ||
        !(game->prizes.owned & (1u << s->prize)) ||
        s->elapsed_ticks > JELLI_COLLECTIBLE_PROGRESS_TICKS ||
        (s->active && s->pet_id != game->pets[game->active].id) || (!s->active && !s->paused))
        return false;
    bool known = false;
    for (unsigned i = 0u; i < game->count; ++i)
        known = known || game->pets[i].id == s->pet_id;
    if (!known)
        return false;
    return session_mode_valid(s);
}

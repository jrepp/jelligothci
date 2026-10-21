#include "jelli/collectible.h"

JelliResult jelli_collectible_bubble_control(JelliGame *game, unsigned control)
{
    if (!jelli_game_valid(game) || game->collectible.mode != JELLI_COLLECTIBLE_BUBBLE ||
        !game->collectible.active || game->resuming ||
        (control > JELLI_COLLECTIBLE_LEFT && control < JELLI_COLLECTIBLE_NORMAL) ||
        control > JELLI_COLLECTIBLE_MANUAL)
        return JELLI_NOT_READY;
    JelliCollectibleSession *s = &game->collectible;
    if (control >= JELLI_COLLECTIBLE_NORMAL) {
        s->bubble.assist = (uint8_t)(control - JELLI_COLLECTIBLE_NORMAL);
        s->bubble.ticks = 0u;
    } else {
        if (s->paused || s->bubble.input_count == JELLI_BUBBLE_INPUTS)
            return JELLI_NOT_READY;
        s->bubble.input[s->bubble.input_count++] = (uint8_t)control;
    }
    ++game->revision;
    return JELLI_OK;
}

void jelli_collectible_bubble_step(JelliBubble *lab)
{
    bool dropped = false;
    if (lab->input_count) {
        unsigned control = lab->input[0];
        --lab->input_count;
        for (unsigned i = 0u; i < lab->input_count; ++i)
            lab->input[i] = lab->input[i + 1u];
        if (control == JELLI_COLLECTIBLE_DOWN) {
            jelli_bubble_hard_drop(lab);
            dropped = true;
        } else {
            (void)jelli_bubble_move(lab,
                                    control == JELLI_COLLECTIBLE_LEFT    ? -1
                                    : control == JELLI_COLLECTIBLE_RIGHT ? 1
                                                                         : 0,
                                    control == JELLI_COLLECTIBLE_UP);
        }
    }
    if (!dropped)
        jelli_bubble_tick(lab);
    if (lab->status != JELLI_BUBBLE_RUNNING)
        lab->input_count = 0u;
}

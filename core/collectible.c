#include "jelli/collectible.h"
#include "game_internal.h"
#include "jelli/behavior.h"
#include "jelli/collection.h"
#include <stddef.h>

_Static_assert(sizeof(JelliCollectibleSession) <= 120u,
               "Checkpoint working storage exceeds budget");
_Static_assert(sizeof(JelliCollectibleProgress) <= 44u,
               "Per-pet collectible progress exceeds budget");

const JelliCraftRecipe jelli_craft_recipes[] = {
    {.key = "R01", .first = 8u, .second = 4u, .output = JELLI_COLLECTIBLE_CHAIN_PRIZE},
    {.key = "R09", .first = 2u, .second = 3u, .output = JELLI_COLLECTIBLE_RECORD_PRIZE},
    {.key = "R12", .first = 0u, .second = 7u, .output = 12u},
    {.key = "R13", .first = 2u, .second = 8u, .output = 13u},
    {.key = "R14", .first = 4u, .second = 5u, .output = 14u},
    {.key = "R15", .first = 3u, .second = 8u, .output = 15u},
    {.key = "R16", .first = 0u, .second = 5u, .output = 16u},
    {.key = "R17", .first = 4u, .second = 7u, .output = 17u},
    {.key = "R02", .first = 1u, .second = 5u, .output = JELLI_COLLECTIBLE_BUBBLE_PRIZE}};
const unsigned jelli_craft_recipe_count =
    sizeof(jelli_craft_recipes) / sizeof(jelli_craft_recipes[0]);

static uint8_t mode_for(unsigned prize)
{
    return prize == JELLI_COLLECTIBLE_BUBBLE_PRIZE  ? JELLI_COLLECTIBLE_BUBBLE
           : prize == JELLI_COLLECTIBLE_CHAIN_PRIZE ? JELLI_COLLECTIBLE_CHAIN
           : prize >= JELLI_COLLECTIBLE_RECORD_PRIZE &&
                   prize < JELLI_COLLECTIBLE_RECORD_PRIZE + JELLI_COLLECTIBLE_RECORD_COUNT
               ? JELLI_COLLECTIBLE_RECORD
               : JELLI_COLLECTIBLE_GENERIC;
}

JelliResult jelli_collectible_available(const JelliGame *game, unsigned prize)
{
    if (!jelli_game_valid(game) || prize >= jelli_prize_count)
        return JELLI_INVALID_TARGET;
    if (game->resuming)
        return JELLI_BUSY;
    if (!(game->prizes.owned & (1u << prize)))
        return JELLI_NO_ITEM;
    const JelliPet *pet = &game->pets[game->active];
    if (pet->asleep)
        return JELLI_ASLEEP;
    return pet->activity == JELLI_IDLE ? JELLI_OK : JELLI_BUSY;
}

JelliResult jelli_collectible_recipe_available(const JelliGame *game, unsigned recipe)
{
    if (!jelli_game_valid(game) || recipe >= jelli_craft_recipe_count)
        return JELLI_INVALID_TARGET;
    if (game->resuming || game->collectible.active)
        return JELLI_BUSY;
    const JelliCraftRecipe *r = &jelli_craft_recipes[recipe];
    if (r->output >= jelli_prize_count)
        return JELLI_NOT_READY;
    if (game->prizes.owned & (1u << r->output))
        return JELLI_FULL;
    unsigned ingredients = (1u << r->first) | (1u << r->second);
    return (game->prizes.owned & ingredients) == ingredients ? JELLI_OK : JELLI_NO_ITEM;
}

JelliResult jelli_collectible_craft(JelliGame *game, unsigned recipe)
{
    JelliResult result = jelli_collectible_recipe_available(game, recipe);
    if (result != JELLI_OK)
        return result;
    unsigned output = jelli_craft_recipes[recipe].output;
    game->prizes.owned |= (uint32_t)(1u << output);
    game->prizes.discovered |= (uint32_t)(1u << output);
    game->prizes.origin_pet[output] = game->pets[game->active].id;
    if (game->prizes.offered == output + 1u) {
        game->prizes.offered = 0u;
        game->prizes.offered_pet = 0u;
    }
    jelli_collection_unlock(game);
    ++game->revision;
    return JELLI_OK;
}

bool jelli_collectible_special_pair(const JelliPet *pet, unsigned prize)
{
    /* Authored first pairings: starting Jelli loves Chain; TEA loves Morning Tea.
     * Collection identity, rather than form, keeps this stable across evolution. */
    return pet && ((pet->collection_entry == 1u && prize == JELLI_COLLECTIBLE_CHAIN_PRIZE) ||
                   (pet->collection_entry == 7u && prize == JELLI_COLLECTIBLE_RECORD_PRIZE));
}

static void special_reaction(JelliPet *pet)
{
    pet->reaction = JELLI_REACTION_TOUCH_HAPPY;
    pet->reaction_ticks =
        (uint8_t)(jelli_behavior_touch_ticks_max < 10u ? jelli_behavior_touch_ticks_max : 10u);
}

unsigned jelli_cartridge_index(unsigned prize)
{
    return prize == JELLI_COLLECTIBLE_CHAIN_PRIZE    ? 0u
           : prize == JELLI_COLLECTIBLE_BUBBLE_PRIZE ? 1u
                                                     : JELLI_CARTRIDGE_COUNT;
}

static void count_play(JelliGame *game, unsigned prize)
{
    unsigned index = jelli_cartridge_index(prize);
    if (index < JELLI_CARTRIDGE_COUNT && game->cartridge_stats[index].plays < UINT16_MAX)
        ++game->cartridge_stats[index].plays;
}

void jelli_cartridge_count_win(JelliGame *game)
{
    unsigned index = jelli_cartridge_index(game->collectible.prize);
    if (index < JELLI_CARTRIDGE_COUNT && game->cartridge_stats[index].wins < UINT16_MAX)
        ++game->cartridge_stats[index].wins;
}

JelliResult jelli_collectible_play(JelliGame *game, unsigned prize)
{
    JelliResult result = jelli_collectible_available(game, prize);
    if (result != JELLI_OK)
        return result;
    JelliPet *pet = &game->pets[game->active];
    JelliCollectibleSession *s = &game->collectible;
    if (s->pet_id == pet->id && s->prize == prize) {
        s->active = true;
        s->paused = true;
    } else {
        uint32_t generation = s->generation == UINT32_MAX ? 1u : s->generation + 1u;
        *s = (JelliCollectibleSession){.pet_id = pet->id,
                                       .prize = (uint8_t)prize,
                                       .mode = mode_for(prize),
                                       .active = true,
                                       .generation = generation};
        count_play(game, prize);
        if (s->mode == JELLI_COLLECTIBLE_CHAIN)
            jelli_chain_init(&s->chain, false);
        if (s->mode == JELLI_COLLECTIBLE_BUBBLE)
            jelli_bubble_init(&s->bubble, JELLI_BUBBLE_NORMAL);
    }
    if (pet->collectible_progress.special_unlocked & (1u << prize))
        special_reaction(pet);
    ++game->revision;
    return JELLI_OK;
}

static bool finished(const JelliCollectibleSession *s)
{
    return s->mode == JELLI_COLLECTIBLE_BUBBLE   ? s->bubble.status != JELLI_BUBBLE_RUNNING
           : s->mode == JELLI_COLLECTIBLE_CHAIN  ? s->chain.status != JELLI_CHAIN_RUNNING
           : s->mode == JELLI_COLLECTIBLE_RECORD ? s->record_cursor == JELLI_RECORD_SAMPLES
                                                 : s->elapsed_ticks >= 80u;
}

static JelliResult assist_control(JelliGame *game, unsigned control)
{
    JelliCollectibleSession *s = &game->collectible;
    if (s->mode == JELLI_COLLECTIBLE_BUBBLE)
        return jelli_collectible_bubble_control(game, control);
    if (s->mode != JELLI_COLLECTIBLE_CHAIN || control == JELLI_COLLECTIBLE_MANUAL)
        return JELLI_NOT_READY;
    s->chain.relaxed = control == JELLI_COLLECTIBLE_RELAXED;
    s->chain.step_ticks = 0u;
    ++game->revision;
    return JELLI_OK;
}

static void restart_session(JelliCollectibleSession *s)
{
    s->generation = s->generation == UINT32_MAX ? 1u : s->generation + 1u;
    s->elapsed_ticks = 0u;
    s->record_cursor = 0u;
    if (s->mode == JELLI_COLLECTIBLE_CHAIN)
        jelli_chain_init(&s->chain, s->chain.relaxed);
    if (s->mode == JELLI_COLLECTIBLE_BUBBLE)
        jelli_bubble_retry(&s->bubble);
}

JelliResult jelli_collectible_control(JelliGame *game, unsigned control)
{
    if (!jelli_game_valid(game) || control > JELLI_COLLECTIBLE_MANUAL)
        return JELLI_INVALID_TARGET;
    JelliCollectibleSession *s = &game->collectible;
    if (game->resuming || !s->active || s->pet_id != game->pets[game->active].id)
        return JELLI_NOT_READY;
    if (s->mode == JELLI_COLLECTIBLE_BUBBLE && control <= JELLI_COLLECTIBLE_LEFT)
        return jelli_collectible_bubble_control(game, control);
    if (control <= JELLI_COLLECTIBLE_LEFT)
        return !s->paused && s->mode == JELLI_COLLECTIBLE_CHAIN &&
                       jelli_chain_turn(&s->chain, control)
                   ? JELLI_OK
                   : JELLI_NOT_READY;
    if (control == JELLI_COLLECTIBLE_CLOSE || control == JELLI_COLLECTIBLE_PAUSE) {
        s->paused = true;
        s->bubble.input_count = 0u;
        s->active = control != JELLI_COLLECTIBLE_CLOSE;
    } else if (control >= JELLI_COLLECTIBLE_NORMAL) {
        return assist_control(game, control);
    } else {
        JelliResult result = jelli_collectible_available(game, s->prize);
        if (result != JELLI_OK)
            return result;
        if (control == JELLI_COLLECTIBLE_RESTART) {
            if (s->mode != JELLI_COLLECTIBLE_BUBBLE || s->bubble.status == JELLI_BUBBLE_WON)
                count_play(game, s->prize);
            restart_session(s);
        } else if (finished(s)) {
            return JELLI_NOT_READY;
        }
        s->bubble.input_count = 0u;
        s->paused = false;
    }
    ++game->revision;
    return JELLI_OK;
}

static void credit_play(JelliGame *game, unsigned ticks)
{
    JelliPet *pet = &game->pets[game->active];
    unsigned prize = game->collectible.prize;
    unsigned total = pet->collectible_progress.play_ticks[prize] + ticks;
    pet->collectible_progress.play_ticks[prize] =
        (uint16_t)(total > JELLI_COLLECTIBLE_PROGRESS_TICKS ? JELLI_COLLECTIBLE_PROGRESS_TICKS
                                                            : total);
    if (total >= JELLI_COLLECTIBLE_PROGRESS_TICKS && jelli_collectible_special_pair(pet, prize) &&
        !(pet->collectible_progress.special_unlocked & (1u << prize))) {
        pet->collectible_progress.special_unlocked |= (uint16_t)(1u << prize);
        special_reaction(pet);
    }
}

void jelli_collectible_tick(JelliGame *game)
{
    if (!jelli_game_valid(game) || game->resuming || !game->collectible.active ||
        game->collectible.paused)
        return;
    JelliCollectibleSession *s = &game->collectible;
    const JelliPet *pet = &game->pets[game->active];
    if (s->pet_id != pet->id || pet->asleep || pet->activity != JELLI_IDLE) {
        s->paused = true;
        s->bubble.input_count = 0u;
        return;
    }
    if (s->mode == JELLI_COLLECTIBLE_RECORD)
        return;
    credit_play(game, 1u);
    if (s->elapsed_ticks < JELLI_COLLECTIBLE_PROGRESS_TICKS)
        ++s->elapsed_ticks;
    if (s->mode == JELLI_COLLECTIBLE_CHAIN) {
        unsigned length = s->chain.length;
        jelli_chain_tick(&s->chain);
        if (s->chain.status == JELLI_CHAIN_WON)
            jelli_cartridge_count_win(game);
        if (s->chain.length > length || s->chain.status == JELLI_CHAIN_WON)
            special_reaction(&game->pets[game->active]);
    }
    if (s->mode == JELLI_COLLECTIBLE_BUBBLE) {
        unsigned score = s->bubble.score;
        jelli_collectible_bubble_step(&s->bubble);
        if (s->bubble.status == JELLI_BUBBLE_WON)
            jelli_cartridge_count_win(game);
        if (s->bubble.score > score)
            special_reaction(&game->pets[game->active]);
    }
    if (finished(s)) {
        s->paused = true;
        if (s->mode == JELLI_COLLECTIBLE_GENERIC)
            jelli_prize_complete(game, JELLI_PRIZE_PLAY);
    }
    ++game->revision;
}

JelliResult jelli_collectible_record_progress(JelliGame *game, uint32_t sample_cursor)
{
    if (!jelli_game_valid(game) || sample_cursor > JELLI_RECORD_SAMPLES)
        return JELLI_INVALID_TARGET;
    JelliCollectibleSession *s = &game->collectible;
    if (game->resuming || !s->active || s->paused || s->mode != JELLI_COLLECTIBLE_RECORD ||
        s->pet_id != game->pets[game->active].id || game->pets[game->active].asleep ||
        game->pets[game->active].activity != JELLI_IDLE)
        return JELLI_NOT_READY;
    if (sample_cursor < s->record_cursor)
        return JELLI_INVALID_TARGET;
    unsigned ticks = sample_cursor / (JELLI_RECORD_RATE / 10u);
    unsigned before_ticks = game->pets[game->active].collectible_progress.play_ticks[s->prize];
    credit_play(game, ticks - s->elapsed_ticks);
    jelli_collectible_record_reward(game, before_ticks);
    s->elapsed_ticks = (uint16_t)ticks;
    s->record_cursor = sample_cursor;
    if (sample_cursor == JELLI_RECORD_SAMPLES)
        s->paused = true;
    ++game->revision;
    return JELLI_OK;
}

JelliResult jelli_collectible_command(JelliGame *game, JelliCommand command)
{
    if (command.kind == JELLI_CMD_COLLECTIBLE_PLAY)
        return jelli_collectible_play(game, command.value);
    if (command.kind == JELLI_CMD_COLLECTIBLE_CRAFT)
        return jelli_collectible_craft(game, command.value);
    if (command.kind == JELLI_CMD_COLLECTIBLE_CONTROL)
        return jelli_collectible_control(game, command.value);
    return JELLI_INVALID_TARGET;
}

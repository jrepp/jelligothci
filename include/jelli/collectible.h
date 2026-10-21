#ifndef JELLI_COLLECTIBLE_H
#define JELLI_COLLECTIBLE_H
#include "jelli/game.h"

typedef struct {
    const char *key;
    uint8_t first, second, output;
} JelliCraftRecipe;
extern const JelliCraftRecipe jelli_craft_recipes[];
extern const unsigned jelli_craft_recipe_count;

/* Commands take zero-based prize/recipe IDs. Owned ingredients remain owned. */
JelliResult jelli_collectible_play(JelliGame *game, unsigned prize);
JelliResult jelli_collectible_craft(JelliGame *game, unsigned recipe);
JelliResult jelli_collectible_control(JelliGame *game, unsigned control);
JelliResult jelli_collectible_available(const JelliGame *game, unsigned prize);
JelliResult jelli_collectible_recipe_available(const JelliGame *game, unsigned recipe);
/* Absolute actual played cursor at22050Hz. No wall-time/queued-audio credit.
 * Cursor may only increase during unpaused playback; completing661500samples
 * grants the once-per-pet first-listen reward. Muted consumed samples count. */
JelliResult jelli_collectible_record_progress(JelliGame *game, uint32_t sample_cursor);
/* Called by admitted live100ms game ticks only, never offline catch-up. */
JelliResult jelli_collectible_bubble_control(JelliGame *game, unsigned control);
unsigned jelli_cartridge_index(unsigned prize);
void jelli_cartridge_count_win(JelliGame *game);
void jelli_collectible_bubble_step(JelliBubble *lab);
void jelli_collectible_tick(JelliGame *game);
bool jelli_collectible_valid(const JelliGame *game);
bool jelli_collectible_progress_valid(const JelliCollectibleProgress *progress);
bool jelli_collectible_special_pair(const JelliPet *pet, unsigned prize);

/* Independent bounded chain simulation; failed turns preserve state. */
void jelli_chain_init(JelliChain *chain, bool relaxed);
bool jelli_chain_valid(const JelliChain *chain);
bool jelli_chain_turn(JelliChain *chain, unsigned direction);
void jelli_chain_tick(JelliChain *chain);
#endif

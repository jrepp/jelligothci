#define SDL_MAIN_HANDLED
#include "keyboard.h"
#include "jelli/collectible.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static JelliPetEngine pet;
int main(void)
{
    jelli_game_init(&pet.game);
    pet.game.prizes.owned = pet.game.prizes.discovered = JELLI_PRIZE_MASK;
    for (unsigned i = 0u; i < jelli_prize_count; ++i)
        pet.game.prizes.origin_pet[i] = 1u;
    CHECK(jelli_collectible_play(&pet.game, JELLI_COLLECTIBLE_CHAIN_PRIZE) == JELLI_OK);
    jelli_game_advance(&pet.game, JELLI_CHAIN_STEP_TICKS * 100u);
    CHECK(pet.game.collectible.chain.body[0] == 28u);
    jelli_sdl_focus_lost(&pet);
    CHECK(!pet.paused && pet.game.collectible.paused && pet.ui.collectible_menu);
    jelli_game_advance(&pet.game, JELLI_CHAIN_STEP_TICKS * 100u);
    CHECK(pet.game.collectible.chain.body[0] == 28u);
    CHECK(jelli_collectible_control(&pet.game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    jelli_game_advance(&pet.game, JELLI_CHAIN_STEP_TICKS * 100u);
    CHECK(pet.game.collectible.chain.body[0] == 29u);
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = SDLK_UP;
    JelliInput input = {0};
    CHECK(jelli_sdl_keyboard(&pet, &event, &input));
    CHECK(input.kind == JELLI_SWIPE && input.y == -80 && !input.x);
    pet.paused = true;
    jelli_sdl_focus_lost(&pet);
    CHECK(pet.paused); /* Explicit Space pause remains independent. */
    return 0;
}

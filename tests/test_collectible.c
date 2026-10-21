#include "game_fixture.h"
#include "save_layout.h"
#include "jelli/collectible.h"
#include "jelli/save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void own(JelliGame *game, unsigned prize)
{
    jelli_prize_award(game, prize);
    CHECK(jelli_prize_catch(game) == JELLI_OK);
}

static void ticks(JelliGame *game, unsigned count)
{
    for (unsigned i = 0u; i < count; ++i)
        jelli_game_advance(game, 100u);
}

static void chain_step(JelliChain *chain)
{
    for (unsigned i = 0u; i < (chain->relaxed ? 8u : 4u); ++i)
        jelli_chain_tick(chain);
    CHECK(jelli_chain_valid(chain));
}

static void chain_rules(void)
{
    JelliChain chain;
    jelli_chain_init(&chain, false);
    CHECK(!jelli_chain_turn(&chain, JELLI_CHAIN_LEFT));
    CHECK(jelli_chain_turn(&chain, JELLI_CHAIN_UP));
    CHECK(!jelli_chain_turn(&chain, JELLI_CHAIN_LEFT));
    CHECK(!jelli_chain_turn(&chain, 9u));
    chain_step(&chain);
    CHECK(chain.body[0] == 19u && !chain.turned);
    chain = (JelliChain){.body = {9u, 8u, 16u, 17u},
                         .length = 4u,
                         .direction = JELLI_CHAIN_RIGHT,
                         .pending_direction = JELLI_CHAIN_RIGHT,
                         .pickup = 40u};
    CHECK(jelli_chain_turn(&chain, JELLI_CHAIN_DOWN));
    chain_step(&chain); /* The departing tail cell is legal when not growing. */
    CHECK(chain.body[0] == 17u && chain.status == JELLI_CHAIN_RUNNING);
    jelli_chain_init(&chain, true);
    for (unsigned i = 0u; i < 7u; ++i)
        jelli_chain_tick(&chain);
    CHECK(chain.body[0] == 27u);
    jelli_chain_tick(&chain);
    CHECK(chain.body[0] == 28u);
    for (unsigned i = 0u; i < 8u; ++i)
        chain_step(&chain);
    CHECK(chain.status == JELLI_CHAIN_LOST);
}

static uint8_t rotate_cell(unsigned cell, unsigned turns)
{
    unsigned x = cell % JELLI_CHAIN_GRID, y = cell / JELLI_CHAIN_GRID;
    for (unsigned i = 0u; i < turns; ++i) {
        unsigned old_x = x;
        x = JELLI_CHAIN_GRID - 1u - y;
        y = old_x;
    }
    return (uint8_t)(y * JELLI_CHAIN_GRID + x);
}

static void reachable_star_and_win(void)
{
    for (unsigned rotation = 0u; rotation < 4u; ++rotation) {
        JelliChain chain = {.body = {5u, 4u, 3u, 2u, 1u, 0u},
                            .length = 6u,
                            .direction = (uint8_t)((JELLI_CHAIN_RIGHT + rotation) % 4u),
                            .pickup = rotate_cell(6u, rotation)};
        chain.pending_direction = chain.direction;
        for (unsigned i = 0u; i < chain.length; ++i)
            chain.body[i] = rotate_cell(chain.body[i], rotation);
        chain_step(&chain);
        CHECK(chain.length == 7u && chain.status == JELLI_CHAIN_RUNNING);
        CHECK(chain.pickup == rotate_cell(7u, rotation));
        chain_step(&chain);
        CHECK(chain.status == JELLI_CHAIN_WON); /* Seven alone did not win; star did. */
    }
    JelliChain corner = {.body = {1u, 2u, 3u, 4u, 5u, 6u},
                         .length = 6u,
                         .direction = JELLI_CHAIN_LEFT,
                         .pending_direction = JELLI_CHAIN_LEFT,
                         .pickup = 0u};
    chain_step(&corner);
    CHECK(corner.length == 7u && corner.pickup == 8u);
    CHECK(jelli_chain_turn(&corner, JELLI_CHAIN_DOWN));
    chain_step(&corner);
    CHECK(corner.status == JELLI_CHAIN_WON);
    JelliChain trapped = {.body = {1u, 9u, 8u, 16u, 17u, 18u},
                          .length = 6u,
                          .direction = JELLI_CHAIN_LEFT,
                          .pending_direction = JELLI_CHAIN_LEFT,
                          .pickup = 0u};
    chain_step(&trapped);
    CHECK(trapped.length == 7u && trapped.status == JELLI_CHAIN_LOST);
}

static void permanent_play_crafting_and_bow(void)
{
    JelliGame game;
    test_game_pair(&game);
    own(&game, 4u);
    CHECK(jelli_collectible_recipe_available(&game, 0u) == JELLI_NO_ITEM);
    CHECK(jelli_collectible_craft(&game, 99u) == JELLI_INVALID_TARGET);
    CHECK(jelli_collectible_play(&game, 4u) == JELLI_OK); /* Catcher may play. */
    for (unsigned run = 0u; run < 3u; ++run) {
        if (run)
            CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
        ticks(&game, 80u);
        CHECK(game.collectible.paused && (game.prizes.owned & 16u));
    }
    CHECK(game.prizes.offered == 9u);
    CHECK(jelli_prize_catch(&game) == JELLI_OK);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_CLOSE) == JELLI_OK);
    CHECK(jelli_collectible_craft(&game, 0u) == JELLI_OK);
    CHECK(jelli_collectible_craft(&game, 0u) == JELLI_FULL);
    CHECK((game.prizes.owned & ((1u << 8u) | 16u)) == ((1u << 8u) | 16u));
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 2u}) == JELLI_OK);
    CHECK(jelli_collectible_play(&game, JELLI_COLLECTIBLE_CHAIN_PRIZE) == JELLI_OK);
    CHECK(!jelli_collectible_special_pair(&game.pets[1], JELLI_COLLECTIBLE_CHAIN_PRIZE));
    CHECK(jelli_game_valid(&game));
}

static void pause_resume_and_bounded_time(void)
{
    JelliGame game;
    test_game_pair(&game);
    own(&game, JELLI_COLLECTIBLE_CHAIN_PRIZE);
    CHECK(jelli_collectible_play(&game, JELLI_COLLECTIBLE_CHAIN_PRIZE) == JELLI_OK);
    ticks(&game, 1u);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_PAUSE) == JELLI_OK);
    JelliChain before = game.collectible.chain;
    ticks(&game, 20u);
    CHECK(memcmp(&before, &game.collectible.chain, sizeof(before)) == 0);
    CHECK(game.pets[0].collectible_progress.play_ticks[10] == 1u);
    jelli_game_resume_begin(&game, 5000u);
    while (!jelli_game_resume_step(&game)) {
    }
    CHECK(game.pets[0].collectible_progress.play_ticks[10] == 1u);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    game.pets[0].collectible_progress.play_ticks[10] = 599u;
    ticks(&game, 1u);
    CHECK(game.pets[0].collectible_progress.special_unlocked & (1u << 10u));
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    uint32_t generation = game.collectible.generation;
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    CHECK(game.collectible.generation != generation);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, 1u, 0u}) == JELLI_BUSY);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_CLOSE) == JELLI_OK);
    CHECK(jelli_collectible_play(&game, 10u) == JELLI_OK && game.collectible.paused);
}

static void record_samples_and_reward_once(void)
{
    JelliGame game;
    test_game_pair(&game);
    own(&game, 2u);
    own(&game, 3u);
    CHECK(jelli_collectible_craft(&game, 1u) == JELLI_OK);
    CHECK(jelli_collectible_play(&game, 11u) == JELLI_OK);
    ticks(&game, 350u);
    CHECK(game.collectible.record_cursor == 0u && game.collectible.elapsed_ticks == 0u);
    CHECK(jelli_collectible_record_progress(&game, JELLI_RECORD_SAMPLES - 1u) == JELLI_OK);
    CHECK(!game.pets[0].collectible_progress.record_rewarded);
    uint16_t morale = game.pets[0].needs[JELLI_AMUSEMENT];
    CHECK(jelli_collectible_record_progress(&game, JELLI_RECORD_SAMPLES) == JELLI_OK);
    CHECK(game.collectible.paused && game.collectible.active);
    CHECK(game.pets[0].needs[JELLI_AMUSEMENT] == morale + 30u);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    CHECK(jelli_collectible_record_progress(&game, 100u) == JELLI_OK);
    CHECK(jelli_collectible_record_progress(&game, 99u) == JELLI_INVALID_TARGET);
    CHECK(jelli_collectible_record_progress(&game, JELLI_RECORD_SAMPLES + 1u) ==
          JELLI_INVALID_TARGET);
    CHECK(jelli_collectible_record_progress(&game, JELLI_RECORD_SAMPLES) == JELLI_OK);
    CHECK(game.pets[0].needs[JELLI_AMUSEMENT] == morale + 30u);
    CHECK(game.pets[0].collectible_progress.play_ticks[11] == 600u);
}

static void checkpoint_round_trip(void)
{
    JelliSave save = {0}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    test_game_pair(&save.game);
    own(&save.game, 11u);
    CHECK(jelli_collectible_play(&save.game, 11u) == JELLI_OK);
    CHECK(jelli_collectible_record_progress(&save.game, 12345u) == JELLI_OK);
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.collectible.paused && loaded.game.collectible.record_cursor == 12345u);
    CHECK(loaded.game.collectible.generation == save.game.collectible.generation);
    CHECK(jelli_collectible_record_progress(&loaded.game, 13000u) == JELLI_NOT_READY);
    CHECK(jelli_collectible_control(&loaded.game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    CHECK(jelli_collectible_record_progress(&loaded.game, JELLI_RECORD_SAMPLES) == JELLI_OK);
    size = jelli_save_encode(&loaded, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&save, bytes, size));
    uint16_t morale = save.game.pets[0].needs[JELLI_AMUSEMENT];
    CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    CHECK(jelli_collectible_record_progress(&save.game, JELLI_RECORD_SAMPLES) == JELLI_OK);
    CHECK(save.game.pets[0].needs[JELLI_AMUSEMENT] == morale);
    CHECK(sizeof(JelliGame) < JELLI_SAVE_CAPACITY);
    printf("Collectible session: %zu; progress/pet: %zu; game: %zu\n",
           sizeof(JelliCollectibleSession), sizeof(JelliCollectibleProgress), sizeof(JelliGame));
}

static void repair(uint8_t *bytes, size_t size)
{
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (i * 8u));
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(~crc >> (i * 8u));
}

static void legacy_and_corrupt_checkpoint(void)
{
    JelliSave save = {0}, loaded = {0};
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    test_game_pair(&save.game);
    own(&save.game, 4u);
    save.game.prizes.owned = 0u; /* Historical gift was consumed, discovery remains. */
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size);
    size_t extra = TEST_SAVE_V16_BYTES(save.game.count);
    bytes[size - 8u - extra] = 2u; /* Unknown independently versioned checkpoint. */
    repair(bytes, size);
    loaded.sequence = 99u;
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == 99u);
    size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size);
    memmove(bytes + size - 8u - extra, bytes + size - 8u, 8u);
    size -= extra;
    bytes[4] = 15u;
    repair(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.prizes.owned == 0u && loaded.game.prizes.discovered == 16u);
    CHECK(!loaded.game.collectible.pet_id &&
          !loaded.game.pets[0].collectible_progress.record_rewarded);
}

int main(void)
{
    chain_rules();
    reachable_star_and_win();
    permanent_play_crafting_and_bow();
    pause_resume_and_bounded_time();
    record_samples_and_reward_once();
    checkpoint_round_trip();
    legacy_and_corrupt_checkpoint();
    puts("PASS: permanent play, crafting, chain, records and checkpoint");
    return 0;
}

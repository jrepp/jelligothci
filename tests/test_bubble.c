#include "game_fixture.h"
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

static void land(JelliBubble *lab)
{
    unsigned cursor = lab->cursor, level = lab->level;
    for (unsigned i = 0u; i <= JELLI_BUBBLE_HEIGHT && lab->cursor == cursor &&
                          lab->level == level && lab->status == JELLI_BUBBLE_RUNNING;
         ++i)
        jelli_bubble_drop(lab);
    CHECK(jelli_bubble_valid(lab));
}

static void place(JelliBubble *lab, unsigned column)
{
    CHECK(jelli_bubble_move(lab, 0, true));
    for (unsigned i = 0u; i < JELLI_BUBBLE_WIDTH && lab->x != column; ++i)
        CHECK(jelli_bubble_move(lab, lab->x < column ? 1 : -1, false));
    CHECK(lab->x == column);
    land(lab);
}

static void authored_solutions_and_mistake(void)
{
    for (unsigned mistake = 0u; mistake < 2u; ++mistake) {
        JelliBubble lab;
        jelli_bubble_init(&lab, JELLI_BUBBLE_MANUAL);
        unsigned highest_level = 0u;
        for (unsigned pair = 0u; pair < 60u && lab.status == JELLI_BUBBLE_RUNNING; ++pair) {
            CHECK(jelli_bubble_viruses(&lab) <= (lab.level + 1u) * 3u);
            if (lab.level > highest_level) {
                highest_level = lab.level;
                CHECK(jelli_bubble_viruses(&lab) == (lab.level + 1u) * 3u);
            }
            unsigned column = lab.color * 2u - 1u;
            if (mistake && !pair)
                column = 0u; /* A reasonable misplaced first pair remains recoverable. */
            place(&lab, column);
        }
        CHECK(lab.status == JELLI_BUBBLE_WON && highest_level == 2u);
        CHECK(lab.score >= 1800u && !jelli_bubble_viruses(&lab));
    }
}

static void support_rotation_and_clears(void)
{
    JelliBubble lab;
    jelli_bubble_init(&lab, JELLI_BUBBLE_MANUAL);
    memset(lab.cells, 0, sizeof(lab.cells));
    lab.cells[5u * 6u + 2u] = 6u;
    lab.color = 1u;
    land(&lab); /* Left half lands on a virus; right half must reach the floor. */
    CHECK(lab.cells[4u * 6u + 2u] == 1u && lab.cells[9u * 6u + 3u] == 1u);
    CHECK(lab.cells[4u * 6u + 3u] == 0u && lab.cells[5u * 6u + 2u] == 6u);
    lab.x = 5u;
    lab.vertical = 1u;
    JelliBubble before = lab;
    CHECK(!jelli_bubble_move(&lab, 0, true));
    CHECK(jelli_bubble_same(&before, &lab));
    CHECK(!jelli_bubble_move(&lab, 1, false));
    jelli_bubble_init(&lab, 0u);
    memset(lab.cells, 0, sizeof(lab.cells));
    lab.cells[54] = 5u;
    lab.cells[55] = 1u;
    lab.x = 2u;
    lab.y = 9u;
    lab.color = 1u;
    lab.cursor = JELLI_BUBBLE_PAIRS;
    jelli_bubble_drop(&lab); /* Horizontal four clears before exhausted supply. */
    CHECK(lab.level == 1u && lab.status == JELLI_BUBBLE_RUNNING && lab.score == 130u);
}

static void cascade_and_victory_priority(void)
{
    JelliBubble lab;
    jelli_bubble_init(&lab, JELLI_BUBBLE_MANUAL);
    memset(lab.cells, 0, sizeof(lab.cells));
    lab.cells[54] = 5u;
    for (unsigned row = 6u; row < 9u; ++row)
        lab.cells[(size_t)row * 6u] = 1u;
    lab.cells[30] = 2u;
    lab.cells[55] = 6u;
    lab.cells[56] = lab.cells[57] = 2u;
    lab.x = 4u;
    lab.y = 9u;
    lab.color = 3u;
    jelli_bubble_drop(&lab);
    CHECK(lab.level == 1u && lab.score == 390u); /* Clear, fall, second clear x2. */
    memset(lab.cells, 0, sizeof(lab.cells));
    lab.level = 2u;
    lab.cells[54] = 5u;
    lab.cells[55] = 1u;
    for (unsigned row = 0u; row < JELLI_BUBBLE_HEIGHT; ++row)
        lab.cells[row * 6u + 5u] = (uint8_t)(row % 3u + 1u);
    lab.x = 2u;
    lab.y = 9u;
    lab.color = 1u;
    lab.cursor = JELLI_BUBBLE_PAIRS;
    jelli_bubble_drop(&lab);
    CHECK(lab.status == JELLI_BUBBLE_WON); /* Victory beats danger row and empty supply. */
    CHECK(jelli_bubble_valid(&lab));
}

static void top_and_retry(void)
{
    JelliBubble lab;
    jelli_bubble_init(&lab, 0u);
    lab.level = 1u;
    lab.level_score = 300u;
    lab.score = 430u;
    for (unsigned row = 1u; row < JELLI_BUBBLE_HEIGHT; ++row)
        lab.cells[row * 6u + 2u] = (uint8_t)(row % 3u + 1u);
    lab.color = 3u;
    jelli_bubble_drop(&lab);
    CHECK(lab.status == JELLI_BUBBLE_TOP);
    jelli_bubble_retry(&lab);
    CHECK(lab.level == 1u && lab.score == 300u && jelli_bubble_viruses(&lab) == 6u);
    lab.cursor = JELLI_BUBBLE_PAIRS;
    land(&lab);
    CHECK(lab.status == JELLI_BUBBLE_EMPTY);
    jelli_bubble_retry(&lab);
    CHECK(lab.level == 1u && lab.cursor == 1u);
    lab.assist = JELLI_BUBBLE_MANUAL;
    for (unsigned i = 0u; i < 200u; ++i)
        jelli_bubble_tick(&lab);
    CHECK(lab.y == 0u && lab.ticks == 0u);
}

static void saved_access_and_interruption(void)
{
    static JelliSave save, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    test_game_pair(&save.game);
    save.game.prizes.owned = save.game.prizes.discovered = (1u << 1u) | (1u << 5u);
    save.game.prizes.origin_pet[1] = save.game.pets[0].id;
    save.game.prizes.origin_pet[5] = save.game.pets[0].id;
    CHECK(jelli_collectible_craft(&save.game, 8u) == JELLI_OK);
    CHECK(jelli_collectible_play(&save.game, JELLI_COLLECTIBLE_BUBBLE_PRIZE) == JELLI_OK);
    CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_MANUAL) == JELLI_OK);
    CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_UP) == JELLI_OK);
    CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_DOWN) == JELLI_OK);
    jelli_collectible_tick(&save.game);
    jelli_collectible_tick(&save.game);
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.collectible.paused);
    CHECK(jelli_bubble_same(&loaded.game.collectible.bubble, &save.game.collectible.bubble));
    CHECK(jelli_collectible_control(&loaded.game, JELLI_COLLECTIBLE_DOWN) == JELLI_NOT_READY);
    CHECK(jelli_collectible_control(&loaded.game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    loaded.game.pets[0].asleep = true;
    loaded.game.pets[0].scheduled_sleep = true;
    jelli_collectible_tick(&loaded.game);
    CHECK(loaded.game.collectible.paused);
    loaded.game.pets[0].asleep = false;
    loaded.game.pets[0].scheduled_sleep = false;
    CHECK(jelli_collectible_control(&loaded.game, JELLI_COLLECTIBLE_CLOSE) == JELLI_OK);
    CHECK(jelli_game_command(&loaded.game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 2u}) == JELLI_OK);
    CHECK(jelli_collectible_play(&loaded.game, JELLI_COLLECTIBLE_BUBBLE_PRIZE) == JELLI_OK);
    CHECK(loaded.game.collectible.pet_id == loaded.game.pets[1].id);
    CHECK(!jelli_collectible_special_pair(&loaded.game.pets[1], JELLI_COLLECTIBLE_BUBBLE_PRIZE));
    CHECK(loaded.game.prizes.owned == save.game.prizes.owned);
    loaded.game.collectible.bubble.cells[0] = 4u;
    CHECK(!jelli_game_valid(&loaded.game));
    printf("Bubble runtime %zu, session %zu, encoded pair-pet save %zu\n", sizeof(JelliBubble),
           sizeof(JelliCollectibleSession), size);
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

static void legacy_and_invalid_payload(void)
{
    static JelliSave save, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    test_game_pair(&save.game);
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size > 61u);
    memmove(bytes + size - 61u, bytes + size - 8u, 8u);
    size -= 53u;
    bytes[4] = 17u;
    repair(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(!loaded.game.collectible.pet_id && loaded.game.activity_music_enabled);
    save.game.prizes.owned = save.game.prizes.discovered = 1u << JELLI_COLLECTIBLE_BUBBLE_PRIZE;
    save.game.prizes.origin_pet[JELLI_COLLECTIBLE_BUBBLE_PRIZE] = save.game.pets[0].id;
    CHECK(jelli_collectible_play(&save.game, JELLI_COLLECTIBLE_BUBBLE_PRIZE) == JELLI_OK);
    size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size);
    bytes[size - 61u] = 1u; /* Earlier v18 prototype lacks cartridge statistics. */
    memmove(bytes + size - 17u, bytes + size - 8u, 8u);
    size -= 9u;
    repair(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(jelli_bubble_same(&loaded.game.collectible.bubble, &save.game.collectible.bubble));
    CHECK(!loaded.game.cartridge_stats[1].plays);
    size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size);
    bytes[size - 61u] = 4u; /* Unknown content identity preserves output. */
    repair(bytes, size);
    loaded.sequence = 123u;
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == 123u);
    bytes[size - 61u] = 3u;
    bytes[size - 60u] = 4u; /* Invalid packed cell, valid checksum. */
    repair(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == 123u);
}

static void cartridge_statistics(void)
{
    static JelliSave save, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    jelli_game_init(&save.game);
    save.game.prizes.owned = save.game.prizes.discovered = 1u << JELLI_COLLECTIBLE_BUBBLE_PRIZE;
    save.game.prizes.origin_pet[JELLI_COLLECTIBLE_BUBBLE_PRIZE] = save.game.pets[0].id;
    CHECK(jelli_collectible_play(&save.game, JELLI_COLLECTIBLE_BUBBLE_PRIZE) == JELLI_OK);
    CHECK(save.game.cartridge_stats[1].plays == 1u && !save.game.cartridge_stats[1].wins);
    CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    CHECK(save.game.cartridge_stats[1].plays == 1u); /* Current-level retry is the same run. */
    CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_CLOSE) == JELLI_OK);
    CHECK(jelli_collectible_play(&save.game, JELLI_COLLECTIBLE_BUBBLE_PRIZE) == JELLI_OK);
    CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    CHECK(save.game.cartridge_stats[1].plays == 1u);
    JelliBubble *lab = &save.game.collectible.bubble;
    memset(lab->cells, 0, sizeof(lab->cells));
    lab->level = 2u;
    lab->cells[54] = 5u;
    lab->cells[55] = 1u;
    lab->x = 2u;
    lab->y = 9u;
    lab->color = 1u;
    CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_DOWN) == JELLI_OK);
    jelli_collectible_tick(&save.game);
    jelli_collectible_tick(&save.game);
    CHECK(save.game.cartridge_stats[1].wins == 1u);
    for (unsigned i = 0u; i < 20u; ++i)
        jelli_collectible_tick(&save.game);
    CHECK(save.game.cartridge_stats[1].wins == 1u);
    save.game.activity_music_enabled = false;
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(!loaded.game.activity_music_enabled);
    CHECK(loaded.game.cartridge_stats[1].plays == 1u && loaded.game.cartridge_stats[1].wins == 1u);
    CHECK(jelli_collectible_control(&loaded.game, JELLI_COLLECTIBLE_RESUME) == JELLI_NOT_READY);
    CHECK(jelli_collectible_control(&loaded.game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    CHECK(loaded.game.cartridge_stats[1].plays == 2u && loaded.game.cartridge_stats[1].wins == 1u);
    loaded.game.cartridge_stats[1].plays = UINT16_MAX;
    loaded.game.collectible.bubble.status = JELLI_BUBBLE_WON;
    loaded.game.collectible.bubble.level = 2u;
    memset(loaded.game.collectible.bubble.cells, 0, sizeof(loaded.game.collectible.bubble.cells));
    loaded.game.collectible.paused = true;
    CHECK(jelli_collectible_control(&loaded.game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    CHECK(loaded.game.cartridge_stats[1].plays == UINT16_MAX);
}

static void queued_swipes(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.prizes.owned = game.prizes.discovered = 1u << JELLI_COLLECTIBLE_BUBBLE_PRIZE;
    game.prizes.origin_pet[JELLI_COLLECTIBLE_BUBBLE_PRIZE] = game.pets[0].id;
    CHECK(jelli_collectible_play(&game, JELLI_COLLECTIBLE_BUBBLE_PRIZE) == JELLI_OK);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_MANUAL) == JELLI_OK);
    const unsigned inputs[] = {JELLI_COLLECTIBLE_LEFT, JELLI_COLLECTIBLE_LEFT, JELLI_COLLECTIBLE_UP,
                               JELLI_COLLECTIBLE_DOWN};
    for (unsigned i = 0u; i < 4u; ++i)
        CHECK(jelli_collectible_control(&game, inputs[i]) == JELLI_OK);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_RIGHT) == JELLI_NOT_READY);
    CHECK(game.collectible.bubble.x == 2u && game.collectible.bubble.input_count == 4u);
    jelli_collectible_tick(&game);
    CHECK(game.collectible.bubble.x == 1u && game.collectible.bubble.input_count == 3u);
    jelli_collectible_tick(&game);
    CHECK(game.collectible.bubble.x == 0u && game.collectible.bubble.input_count == 2u);
    jelli_collectible_tick(&game);
    CHECK(game.collectible.bubble.vertical == 1u && game.collectible.bubble.input_count == 1u);
    jelli_collectible_tick(&game);
    CHECK(game.collectible.bubble.cursor == 2u && !game.collectible.bubble.input_count);
    CHECK(game.collectible.bubble.cells[48] == 1u && game.collectible.bubble.cells[54] == 1u);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_LEFT) == JELLI_OK);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_PAUSE) == JELLI_OK);
    CHECK(!game.collectible.bubble.input_count);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_RESUME) == JELLI_OK);
    jelli_collectible_tick(&game);
    CHECK(game.collectible.bubble.x == 2u);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_LEFT) == JELLI_OK);
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_RESTART) == JELLI_OK);
    CHECK(!game.collectible.bubble.input_count);
}

static void timed_falls_remain_saveable(void)
{
    static JelliSave save, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    for (unsigned assist = JELLI_BUBBLE_NORMAL; assist <= JELLI_BUBBLE_RELAXED; ++assist) {
        jelli_game_init(&save.game);
        save.game.prizes.owned = save.game.prizes.discovered = 1u << JELLI_COLLECTIBLE_BUBBLE_PRIZE;
        save.game.prizes.origin_pet[JELLI_COLLECTIBLE_BUBBLE_PRIZE] = save.game.pets[0].id;
        CHECK(jelli_collectible_play(&save.game, JELLI_COLLECTIBLE_BUBBLE_PRIZE) == JELLI_OK);
        CHECK(jelli_collectible_control(&save.game, JELLI_COLLECTIBLE_NORMAL + assist) == JELLI_OK);
        unsigned delay =
            assist == JELLI_BUBBLE_RELAXED ? JELLI_BUBBLE_RELAXED_TICKS : JELLI_BUBBLE_FALL_TICKS;
        for (unsigned tick = 1u; tick <= delay * 20u; ++tick) {
            jelli_game_advance(&save.game, 100u);
            CHECK(jelli_game_valid(&save.game));
            if (tick < delay)
                CHECK(save.game.collectible.bubble.y == 0u);
            if (tick == delay)
                CHECK(save.game.collectible.bubble.y == 1u && !save.game.collectible.bubble.ticks);
            size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
            CHECK(size && jelli_save_decode(&loaded, bytes, size));
        }
        CHECK(save.game.collectible.bubble.cursor > 1u);
    }
}

int main(void)
{
    timed_falls_remain_saveable();
    queued_swipes();
    cartridge_statistics();
    legacy_and_invalid_payload();
    authored_solutions_and_mistake();
    support_rotation_and_clears();
    cascade_and_victory_priority();
    top_and_retry();
    saved_access_and_interruption();
    puts("PASS: Bubble Lab rules, authored solutions, mistake recovery, retry and checkpoint");
    return 0;
}

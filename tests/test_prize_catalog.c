#include "game_fixture.h"
#include "jelli/save.h"
#include "save_layout.h"
#include "jelli/collectible.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* Link a full authored registry in place of the production catalog: exercise the
 * uint16 ownership boundary without adding gameplay content to production. */
const unsigned jelli_prize_count = JELLI_PRIZE_COUNT;
const JelliPrizeDefinition jelli_prizes[JELLI_PRIZE_COUNT] = {
    [15] = {"test-present", "TEST PRESENT", 12000u}};

static void repair_checksum(uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0u; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    crc = ~crc;
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(crc >> (i * 8u));
}

static void capacity_award_and_save(void)
{
    JelliSave save = {0}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    test_game_pair(&save.game);
    jelli_prize_award(&save.game, 15u);
    CHECK(save.game.prizes.offered == 16u);
    CHECK(jelli_prize_catch(&save.game) == JELLI_OK);
    CHECK(save.game.prizes.owned == UINT16_C(0x8000));
    CHECK(save.game.prizes.origin_pet[15] == save.game.pets[0].id);
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && bytes[4] == JELLI_SAVE_VERSION && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.prizes.owned == UINT16_C(0x8000));
    CHECK(loaded.game.prizes.discovered == UINT16_C(0x8000));
    CHECK(loaded.game.prizes.origin_pet[15] == save.game.pets[0].id);
    CHECK(jelli_prize_gift(&loaded.game, 15u) == JELLI_OK);
    CHECK(jelli_collectible_control(&loaded.game, JELLI_COLLECTIBLE_CLOSE) == JELLI_OK);
    CHECK(jelli_game_command(&loaded.game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 2u}) == JELLI_OK);
    CHECK(jelli_prize_gift(&loaded.game, 15u) == JELLI_OK);
    CHECK(loaded.game.prizes.owned == UINT16_C(0x8000) &&
          loaded.game.prizes.discovered == UINT16_C(0x8000));
    CHECK(!loaded.game.prizes.offered); /* Only completed shared play earns a bow. */
    jelli_prize_award(&save.game, JELLI_PRIZE_COUNT);
    CHECK(!save.game.prizes.offered);
}

static void original_v14_teddy_is_preserved(void)
{
    JelliSave save = {0}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    test_game_pair(&save.game);
    jelli_prize_award(&save.game, 9u);
    CHECK(jelli_prize_catch(&save.game) == JELLI_OK);
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size > 32u);
    /* v14 has exactly one post-legacy origin, regardless of the new registry. */
    size_t extension = 24u + TEST_SAVE_V16_BYTES(save.game.count);
    memmove(bytes + size - 8u - extension, bytes + size - 8u, 8u);
    size -= extension;
    bytes[4] = 14u;
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (i * 8u));
    repair_checksum(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.prizes.owned == 512u && loaded.game.prizes.discovered == 512u);
    CHECK(loaded.game.prizes.origin_pet[9] == save.game.pets[0].id);
    for (unsigned i = 10u; i < JELLI_PRIZE_COUNT; ++i)
        CHECK(loaded.game.prizes.origin_pet[i] == 0u);
}

int main(void)
{
    capacity_award_and_save();
    original_v14_teddy_is_preserved();
    puts("PASS: appended prize boundary, gift/save round trip and v14 migration");
    return 0;
}

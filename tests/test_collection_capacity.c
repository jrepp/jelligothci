#include "jelli/collection.h"
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

#include "collection_catalog_fixture.h"

static void repair_record(uint8_t *bytes, size_t size)
{
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (i * 8u));
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

static void existing_discovery_unlocks_appended_family(void)
{
    JelliSave save = {0}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    jelli_game_init(&save.game);
    for (unsigned i = 1u; i < 9u; ++i)
        save.game.pets[i] = jelli_collection_new_pet(i + 1u, i + 1u);
    save.game.count = 9u;
    save.game.prizes.owned = save.game.prizes.discovered = 512u;
    save.game.prizes.origin_pet[9] = 1u;
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.count == 10u && (loaded.game.new_pets & 512u));
    CHECK(loaded.game.prizes.origin_pet[9] == 1u);
    uint8_t before[JELLI_SAVE_CAPACITY], after[JELLI_SAVE_CAPACITY];
    size_t before_size = jelli_save_encode(&loaded, before, sizeof(before));
    CHECK(before_size);
    /* Corrupt unused slot 15's origin, with a valid checksum: reconciliation
     * must not turn malformed input into an accepted save or publish output. */
    bytes[size - 12u - TEST_SAVE_V16_BYTES(save.game.count)] = 1u;
    repair_record(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size));
    CHECK(jelli_save_encode(&loaded, after, sizeof(after)) == before_size);
    CHECK(memcmp(before, after, before_size) == 0);
    size = jelli_save_encode(&save, bytes, sizeof(bytes));
    size_t extension = 24u + TEST_SAVE_V16_BYTES(save.game.count);
    memmove(bytes + size - 8u - extension, bytes + size - 8u, 8u);
    size -= extension;
    bytes[4] = 14u;
    repair_record(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.count == 10u && (loaded.game.new_pets & 512u));
}

int main(void)
{
    existing_discovery_unlocks_appended_family();
    JelliSave save = {0}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    jelli_game_init(&save.game);
    CHECK(save.game.count == 1u);
    for (unsigned i = 0u; i < jelli_prize_count; ++i) {
        jelli_prize_award(&save.game, i);
        CHECK(jelli_prize_catch(&save.game) == JELLI_OK);
    }
    CHECK(save.game.count == 16u && save.game.new_pets == UINT16_C(0xfffe));
    CHECK(jelli_game_valid(&save.game));
    CHECK(jelli_collection_find(&save.game, 16u) == 15);
    CHECK(jelli_collection_set(16u) == &jelli_evolution_sets[0]);
    CHECK(jelli_collection_set(17u) == NULL);
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size > 4096u && size <= JELLI_SAVE_CAPACITY);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.count == 16u && loaded.game.new_pets == UINT16_C(0xfffe));
    CHECK(loaded.game.prizes.discovered == UINT16_MAX);
    CHECK(jelli_game_command(&loaded.game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 16u}) ==
          JELLI_OK);
    CHECK(loaded.game.pets[loaded.game.active].collection_entry == 16u);
    /* Activating directly preserves the unread marker until the UI opens its entry. */
    CHECK(loaded.game.new_pets & UINT16_C(0x8000));
    printf("PASS: sixteen authored families unlock/save/activate; game=%zu save=%zu bytes\n",
           sizeof(JelliGame), size);
    return 0;
}

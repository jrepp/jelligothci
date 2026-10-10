#include "jelli/collection.h"
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

static void discover(JelliGame *game, unsigned prize)
{
    game->prizes.offered = (uint8_t)prize;
    game->prizes.offered_pet = game->pets[game->active].id;
    CHECK(jelli_prize_catch(game) == JELLI_OK);
}

static void unlock_and_persist(void)
{
    JelliSave save = {0}, loaded;
    jelli_game_init(&save.game);
    JelliGame *game = &save.game;
    game->pets[0].id = UINT32_MAX; /* Allocation must not wrap the highest legacy ID. */
    discover(game, 6u);
    CHECK(game->count == 3u && game->active == 0u);
    int index = jelli_collection_find(game, 3u);
    CHECK(index >= 0 && game->pets[index].id != UINT32_MAX);
    CHECK(game->new_pets == 4u);
    uint32_t id = game->pets[index].id;
    jelli_collection_unlock(game);
    CHECK(game->count == 3u && game->pets[index].id == id);
    CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_ACTIVATE, UINT32_MAX, id}) == JELLI_OK);
    CHECK(jelli_prize_gift(game, 5u) == JELLI_OK);
    CHECK(!(game->prizes.owned & 32u) && (game->prizes.discovered & 32u));
    jelli_collection_unlock(game);
    CHECK(game->count == 3u);
    uint64_t stored_ticks = game->pets[0].ticks;
    for (unsigned i = 0u; i < 75u; ++i)
        jelli_game_advance(game, 800u);
    CHECK(game->pets[index].form == 1u && game->pets[index].reached_forms == 3u);
    CHECK(game->pets[index].id == id && game->pets[index].collection_entry == 3u);
    CHECK(game->pets[0].ticks == stored_ticks);
    for (unsigned p = 1u; p <= 9u; ++p) {
        if (!(game->prizes.owned & (1u << (p - 1u))))
            discover(game, p);
    }
    CHECK(game->count == 9u && jelli_game_valid(game));
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size > 0u && size <= 4096u && bytes[4] == JELLI_SAVE_VERSION);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.count == 9u && loaded.game.new_pets == game->new_pets);
    CHECK(loaded.game.pets[index].id == id && loaded.game.pets[index].reached_forms == 3u);
    jelli_collection_unlock(&loaded.game);
    CHECK(loaded.game.count == 9u);
    printf("Nine-pet save: %zu bytes; game: %zu bytes; pet: %zu bytes\n", size, sizeof(JelliGame),
           sizeof(JelliPet));
}

static void repair(uint8_t *bytes, size_t size)
{
    uint32_t total = (uint32_t)size;
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(total >> (8u * i));
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size - 8u; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    crc = ~crc;
    for (unsigned i = 0u; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(crc >> (8u * i));
}

static void legacy_and_invalid_records(void)
{
    JelliSave source = {0}, loaded;
    jelli_game_init(&source.game);
    source.game.count = 8u;
    for (unsigned i = 0u; i < 8u; ++i) {
        source.game.pets[i] = source.game.pets[0];
        source.game.pets[i].id = 100u + i;
        source.game.pets[i].collection_entry = (uint8_t)(i + 1u);
    }
    source.game.active = 7u;
    source.game.pets[7].form = 1u;
    source.game.prizes.owned = source.game.prizes.discovered = 32u;
    source.game.prizes.origin_pet[5] = 103u;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&source, bytes, sizeof(bytes));
    CHECK(size != 0u);
    size_t extension = 2u + source.game.count * 7u;
    memmove(bytes + size - 8u - extension, bytes + size - 8u, 8u);
    size -= extension;
    bytes[4] = 3u;
    repair(bytes, size);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.count == 8u && loaded.game.active == 7u);
    CHECK(loaded.game.pets[7].id == 107u && loaded.game.pets[7].reached_forms == 2u);
    CHECK(loaded.game.prizes.origin_pet[5] == 103u);
    for (unsigned i = 0u; i < 8u; ++i)
        CHECK(loaded.game.pets[i].collection_entry == i + 1u);
    size = jelli_save_encode(&loaded, bytes, sizeof(bytes));
    CHECK(size != 0u);
    JelliSave before = loaded;
    bytes[size - 8u - 56u] = 9u; /* First entry now duplicates an invalid NEW mask below. */
    bytes[size - 8u - 58u] = 1u;
    repair(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size));
    uint8_t before_bytes[JELLI_SAVE_CAPACITY], after_bytes[JELLI_SAVE_CAPACITY];
    size_t before_size = jelli_save_encode(&before, before_bytes, sizeof(before_bytes));
    CHECK(jelli_save_encode(&loaded, after_bytes, sizeof(after_bytes)) == before_size);
    CHECK(before_size != 0u && memcmp(before_bytes, after_bytes, before_size) == 0);
    size = jelli_save_encode(&source, bytes, sizeof(bytes));
    bytes[size - 8u - 56u + 2u] = 1u; /* Duplicate collection binding. */
    repair(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size));
}

int main(void)
{
    unlock_and_persist();
    legacy_and_invalid_records();
    puts("PASS: collection unlocks, identity, evolution, capacity, migration and corrupt saves");
    return 0;
}

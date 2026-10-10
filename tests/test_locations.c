#include "jelli/locations.h"
#include "jelli/activities.h"
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

int main(void)
{
    CHECK(jelli_location_count == 6u && jelli_location_count <= JELLI_LOCATION_CAPACITY);
    CHECK(strcmp(jelli_locations[0].name, "HOME") == 0);
    CHECK(strcmp(jelli_locations[1].name, "GARDEN") == 0);
    JelliSave save = {0}, loaded;
    jelli_game_init(&save.game);
    for (unsigned i = 0u; i < jelli_location_count; ++i) {
        const JelliPet *pet = &save.game.pets[0];
        JelliResult expected = pet->location == i ? JELLI_NOT_READY : JELLI_OK;
        CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_TRAVEL, pet->id, i}) ==
              expected);
        CHECK(pet->location == i && jelli_game_valid(&save.game));
        uint8_t bytes[JELLI_SAVE_CAPACITY];
        size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
        CHECK(size && jelli_save_decode(&loaded, bytes, size));
        CHECK(loaded.game.pets[0].location == i);
    }
    CHECK(jelli_game_command(&save.game,
                             (JelliCommand){JELLI_CMD_TRAVEL, 1u, jelli_location_count}) ==
          JELLI_INVALID_TARGET);
    for (unsigned id = 0u; id < jelli_moment_count; ++id) {
        JelliPet *pet = &save.game.pets[0];
        unsigned seen = 0u;
        for (unsigned i = 0u; i < 256u; ++i) {
            pet->interaction_due =
                (uint64_t)i * 100u + (uint64_t)jelli_moments[id].duration_s * 10u;
            unsigned place = jelli_moment_location(pet, id);
            CHECK(place < jelli_location_count);
            CHECK(jelli_moments[id].locations & (1u << place));
            CHECK(place == jelli_moment_location(pet, id));
            seen |= 1u << place;
        }
        if (jelli_moments[id].randomize_location)
            CHECK(seen == jelli_moments[id].locations);
    }
    puts("PASS: every location supports travel, save/reload and bounded activity selection");
    return 0;
}

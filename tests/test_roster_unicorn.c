/* The unicorn: the default pet for new players, its never-evolving form, SWITCH PET
 * through all four pets, the species helper's rules, and the version 7 save migration
 * that gives v5/v6 three-pet saves exactly one unicorn while keeping their active pet. */
#include "jelli/save.h"

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

/* Save layout: the roster count byte, the first pet record, and the v6 sound level. */
enum { TRAILER = 8u, PREFIX = 71u, COUNT_OFFSET = 68u, SOUND_FROM_END = TRAILER + 1u };
static const size_t PET_SIZE =
    130u + JELLI_HABIT_BIN_COUNT * 6u + 37u + JELLI_PRIZE_COUNT * 2u + 9u;

static unsigned form_count(const JelliGame *game, uint8_t form)
{
    unsigned count = 0u;
    for (unsigned i = 0u; i < game->count && i < JELLI_PET_CAPACITY; ++i)
        count += game->pets[i].form == form ? 1u : 0u;
    return count;
}

/* Field-wise (struct padding is unspecified): the state a pet's care can change. */
static bool same_pet(const JelliPet *a, const JelliPet *b)
{
    return a->id == b->id && a->form == b->form && a->bond == b->bond &&
           a->location == b->location && a->bedtime == b->bedtime && a->ticks == b->ticks &&
           a->stage_ticks == b->stage_ticks && a->asleep == b->asleep &&
           a->scheduled_sleep == b->scheduled_sleep && a->nap_due == b->nap_due &&
           a->health == b->health && a->activity == b->activity &&
           a->phase_offset == b->phase_offset && a->sleep_duration == b->sleep_duration &&
           a->random_state == b->random_state &&
           memcmp(a->needs, b->needs, sizeof(a->needs)) == 0 &&
           memcmp(a->need_remainders, b->need_remainders, sizeof(a->need_remainders)) == 0;
}

/* A just-added unicorn matches the one a new game starts with, apart from its ID. */
static bool fresh_unicorn(const JelliPet *pet, uint32_t id)
{
    JelliGame reference;
    jelli_game_init(&reference);
    JelliPet expected = reference.pets[3];
    expected.id = expected.random_state = id;
    return expected.form == JELLI_FORM_UNICORN && same_pet(pet, &expected);
}

static void repair(uint8_t *bytes, size_t size)
{
    for (unsigned i = 0; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (8u * i));
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < size - TRAILER; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? UINT32_C(0xedb88320) : 0u);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4u; ++i)
        bytes[size - TRAILER + i] = (uint8_t)(crc >> (8u * i));
}

/* Encodes any roster as `version` 5..7: shorter rosters borrow placeholder records
 * from the encoder that are cut back out, and version 5 drops the v6 sound level. */
static size_t encode_as(const JelliSave *save, unsigned version, uint8_t *bytes)
{
    static JelliSave padded;
    padded = *save;
    size_t extra = 0u;
    for (; padded.game.count < JELLI_ROSTER_MIN; extra += PET_SIZE) {
        padded.game.pets[padded.game.count] = padded.game.pets[0];
        padded.game.pets[padded.game.count++].id = 200u + (uint32_t)(extra / PET_SIZE);
    }
    size_t size = jelli_save_encode(&padded, bytes, JELLI_SAVE_CAPACITY);
    CHECK(size > PREFIX + SOUND_FROM_END + extra && bytes[4] == JELLI_SAVE_VERSION);
    size_t cut = PREFIX + (size_t)save->game.count * PET_SIZE;
    memmove(bytes + cut, bytes + cut + extra, size - cut - extra);
    size -= extra;
    bytes[COUNT_OFFSET] = save->game.count;
    if (version < 6u) {
        memmove(bytes + size - SOUND_FROM_END, bytes + size - TRAILER, TRAILER);
        --size;
    }
    bytes[4] = (uint8_t)version;
    repair(bytes, size);
    return size;
}

/* The roster a v5/v6 build wrote: two jellies and the ninja, with `active` chosen. */
static JelliSave three_pet_save(uint8_t active)
{
    JelliSave save = {.sequence = 41u, .anchor_ms = 1700000000000u, .anchor_valid = true};
    jelli_game_init(&save.game);
    save.game.count = 3u;
    save.game.pets[3] = (JelliPet){0};
    save.game.active = active;
    save.game.pets[0].form = JELLI_FORM_LILAC;
    save.game.pets[1].bond = 333u;
    save.game.sound_level = 8u;
    return save;
}

/* Decodes into a sentinel-filled output; a rejection must not change one byte. */
static bool decode_checked(const uint8_t *bytes, size_t size, JelliSave *out)
{
    static JelliSave first, scratch;
    static uint8_t before[sizeof(JelliSave)];
    memset(&first, 0xa5, sizeof(first));
    memcpy(before, &first, sizeof(before));
    if (!jelli_save_decode_workspace(&first, bytes, size, &scratch)) {
        static uint8_t after[sizeof(JelliSave)];
        memcpy(after, &first, sizeof(after)); /* Raw bytes, padding included. */
        CHECK(memcmp(before, after, sizeof(before)) == 0);
        return false;
    }
    *out = first;
    return true;
}

/* A brand-new player starts with the unicorn; the roster itself is unchanged. */
static void new_player_starts_with_unicorn(void)
{
    JelliGame game, roster;
    jelli_game_new(&game);
    jelli_game_init(&roster);
    CHECK(jelli_game_valid(&game) && game.count == 4u && game.active == 3u);
    CHECK(game.pets[game.active].form == JELLI_FORM_UNICORN &&
          game.pets[game.active].id == JELLI_UNICORN_PET_ID);
    CHECK(fresh_unicorn(&game.pets[3], JELLI_UNICORN_PET_ID));
    CHECK(roster.active == 0u && roster.count == game.count && roster.food == game.food &&
          roster.gifts == game.gifts && roster.battery_display == game.battery_display);
    for (uint8_t i = 0u; i < game.count; ++i)
        CHECK(same_pet(&roster.pets[i], &game.pets[i]));
    CHECK(jelli_game_next_slot(&game) == 0u); /* SWITCH PET goes on to the first Mint. */
    jelli_game_new(NULL);
    game.count = (uint8_t)(JELLI_PET_CAPACITY + 1u);
    CHECK(jelli_game_next_slot(&game) == 0u); /* An oversized roster never yields slot 8+. */
    game.active = (uint8_t)(JELLI_PET_CAPACITY - 1u);
    CHECK(jelli_game_next_slot(&game) == 0u); /* Not slot 8, one past the pets array. */
    game.count = JELLI_PET_CAPACITY;          /* A full roster is still switchable, and wraps. */
    CHECK(jelli_game_next_slot(&game) == 0u);
    game.active = (uint8_t)(JELLI_PET_CAPACITY - 2u);
    CHECK(jelli_game_next_slot(&game) == JELLI_PET_CAPACITY - 1u);
}

/* Two offline days as the unicorn never change its form; only its clock runs. */
static void unicorn_never_evolves(void)
{
    JelliGame game;
    jelli_game_new(&game);
    for (unsigned day = 0u; day < 2u; ++day) {
        jelli_game_resume_begin(&game, JELLI_OFFLINE_CAP_MS);
        while (!jelli_game_resume_step(&game))
            CHECK(jelli_game_valid(&game));
        CHECK(game.pets[3].form == JELLI_FORM_UNICORN && jelli_game_valid(&game));
    }
    CHECK(game.pets[3].ticks == UINT64_C(2) * JELLI_DAY_TICKS && game.pets[3].stage_ticks > 600u);
    for (unsigned p = 0u; p < 3u; ++p)
        CHECK(game.pets[p].ticks == 0u && game.pets[p].form == (p < 2u ? 0u : JELLI_FORM_NINJA));
}

/* From the new-player default, SWITCH PET visits 4, 1, 2, 3 and back to 4. */
static void switch_cycles_from_unicorn(void)
{
    JelliGame game;
    jelli_game_new(&game);
    static const uint32_t order[] = {4u, 1u, 2u, 3u, 4u};
    for (unsigned i = 0u; i + 1u < sizeof(order) / sizeof(order[0]); ++i) {
        CHECK(game.pets[game.active].id == order[i]);
        uint8_t next = jelli_game_next_slot(&game);
        JelliCommand activate = {JELLI_CMD_ACTIVATE, game.pets[game.active].id, order[i + 1u]};
        CHECK(jelli_game_command(&game, activate) == JELLI_OK);
        CHECK(game.active == next && game.pets[next].id == order[i + 1u] &&
              jelli_game_valid(&game));
    }
}

static void add_species_untouched(JelliGame *game, uint8_t form)
{
    static uint8_t before[sizeof(JelliGame)], after[sizeof(JelliGame)];
    memcpy(before, game, sizeof(before));
    CHECK(!jelli_game_add_species(game, form));
    memcpy(after, game, sizeof(after)); /* Raw bytes, padding included. */
    CHECK(memcmp(before, after, sizeof(before)) == 0);
}

static JelliGame with_ids(JelliGame game, uint32_t a, uint32_t b, uint32_t c)
{
    game.pets[0].id = game.pets[0].random_state = a;
    game.pets[1].id = game.pets[1].random_state = b;
    game.pets[2].id = game.pets[2].random_state = c;
    return game;
}

/* The unicorn takes ID 4 or the next unused ID above it (never 3), once, and keeps
 * the active slot; non-species forms and invalid or full rosters are refused. */
static void add_unicorn_rules(void)
{
    const JelliGame stored = three_pet_save(1u).game;
    static const struct {
        uint32_t ids[3], expected;
    } cases[] = {{{1u, 2u, 3u}, 4u}, {{1u, 2u, 4u}, 5u}, {{4u, 5u, 6u}, 7u},
                 {{5u, 6u, 7u}, 4u}, {{6u, 4u, 5u}, 7u}, {{9u, 8u, 1u}, 4u}};
    CHECK(!jelli_game_add_species(NULL, JELLI_FORM_UNICORN));
    for (unsigned i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        JelliGame game = with_ids(stored, cases[i].ids[0], cases[i].ids[1], cases[i].ids[2]);
        CHECK(jelli_game_add_species(&game, JELLI_FORM_UNICORN) && game.count == 4u);
        CHECK(fresh_unicorn(&game.pets[3], cases[i].expected) && game.active == 1u);
        CHECK(jelli_game_valid(&game));
        add_species_untouched(&game, JELLI_FORM_UNICORN); /* Never a second unicorn. */
        add_species_untouched(&game, JELLI_FORM_NINJA);   /* It already has a ninja. */
    }
    static const uint8_t not_species[] = {JELLI_FORM_MINT, JELLI_FORM_LILAC, JELLI_FORM_COUNT,
                                          255u};
    JelliGame game = stored;
    for (unsigned i = 0u; i < sizeof(not_species); ++i)
        add_species_untouched(&game, not_species[i]);
    game.count = JELLI_PET_CAPACITY;
    for (unsigned p = 3u; p < JELLI_PET_CAPACITY; ++p) {
        game.pets[p] = game.pets[0];
        game.pets[p].id = game.pets[p].random_state = p + 1u;
    }
    add_species_untouched(&game, JELLI_FORM_UNICORN); /* Full. */
    game = stored;
    game.prizes.offered_pet = JELLI_UNICORN_PET_ID; /* Unknown pet: no rescue by appending. */
    game.prizes.offered = 1u;
    add_species_untouched(&game, JELLI_FORM_UNICORN);
    game = stored;
    game.pets[2].form = JELLI_FORM_UNICORN; /* A unicorn already present. */
    add_species_untouched(&game, JELLI_FORM_UNICORN);
}

/* v5 and v6 saves gain one unicorn after their pets and keep their active pet; v6 keeps
 * its sound level. Re-saving as v7 and reloading never adds a second unicorn. */
static void three_pet_saves_gain_one_unicorn(void)
{
    static uint8_t bytes[JELLI_SAVE_CAPACITY], again[JELLI_SAVE_CAPACITY];
    for (unsigned version = 5u; version <= 6u; ++version) {
        for (uint8_t active = 0u; active < 3u; ++active) {
            JelliSave stored = three_pet_save(active), loaded, reloaded;
            size_t size = encode_as(&stored, version, bytes);
            CHECK(bytes[4] == version && decode_checked(bytes, size, &loaded));
            CHECK(loaded.game.count == 4u && loaded.game.active == active);
            CHECK(fresh_unicorn(&loaded.game.pets[3], JELLI_UNICORN_PET_ID));
            for (unsigned p = 0u; p < 3u; ++p)
                CHECK(same_pet(&loaded.game.pets[p], &stored.game.pets[p]));
            CHECK(loaded.game.sound_level == (version == 6u ? 8u : JELLI_SOUND_LEVEL_DEFAULT));
            CHECK(loaded.sequence == stored.sequence && jelli_game_valid(&loaded.game));
            size = jelli_save_encode(&loaded, bytes, sizeof(bytes));
            CHECK(size != 0u && bytes[4] == 7u && decode_checked(bytes, size, &reloaded));
            CHECK(reloaded.game.count == 4u && reloaded.game.active == active);
            CHECK(form_count(&reloaded.game, JELLI_FORM_UNICORN) == 1u);
            CHECK(jelli_save_encode(&reloaded, again, sizeof(again)) == size);
            CHECK(memcmp(bytes, again, size) == 0); /* A fixed point from the first v7 save. */
        }
    }
}

/* A new game round-trips as v7 unchanged; v7 never migrates, so a v7 roster of fewer
 * than four pets (or without a unicorn slot to spare) is rejected, output untouched. */
static void version_seven_round_trip_and_minimum(void)
{
    static uint8_t bytes[JELLI_SAVE_CAPACITY], again[JELLI_SAVE_CAPACITY];
    JelliSave save = {.sequence = 7u, .anchor_ms = 5u, .anchor_valid = true}, loaded;
    jelli_game_new(&save.game);
    save.game.pets[3].bond = 640u;
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size != 0u && bytes[4] == JELLI_SAVE_VERSION && JELLI_SAVE_VERSION == 7u);
    CHECK(decode_checked(bytes, size, &loaded) && loaded.game.active == 3u);
    CHECK(loaded.game.count == 4u && loaded.game.pets[3].bond == 640u);
    for (unsigned p = 0u; p < 4u; ++p)
        CHECK(same_pet(&loaded.game.pets[p], &save.game.pets[p]));
    CHECK(jelli_save_encode(&loaded, again, sizeof(again)) == size);
    CHECK(memcmp(bytes, again, size) == 0);
    for (uint8_t active = 0u; active < 3u; ++active) {
        JelliSave three = three_pet_save(active);
        size = encode_as(&three, 7u, bytes);
        CHECK(bytes[4] == 7u && !decode_checked(bytes, size, &loaded));
        three.game.pets[2].form = JELLI_FORM_UNICORN; /* Holds a unicorn, but too few pets. */
        size = encode_as(&three, 6u, bytes);
        CHECK(!decode_checked(bytes, size, &loaded));
    }
    /* Only versions 1..7 decode; anything newer is preserved by hosts, not loaded. */
    size = jelli_save_encode(&save, bytes, sizeof(bytes));
    bytes[4] = JELLI_SAVE_VERSION + 1u;
    repair(bytes, size);
    CHECK(!decode_checked(bytes, size, &loaded));
}

int main(void)
{
    new_player_starts_with_unicorn();
    unicorn_never_evolves();
    switch_cycles_from_unicorn();
    add_unicorn_rules();
    three_pet_saves_gain_one_unicorn();
    version_seven_round_trip_and_minimum();
    puts("PASS: unicorn default, never evolves, four-pet SWITCH cycle, v5/v6 to v7 migration");
    return 0;
}

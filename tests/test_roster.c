/* Three-pet roster: the ninja's start, evolution rule, switching order, form validity,
 * and the version 5 save migration that gives older two-pet saves exactly one ninja. */
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

enum { TRAILER = 8u, PREFIX = 71u, SETTING_FROM_END = TRAILER + 1u };

static JelliResult command(JelliGame *game, JelliCommandKind kind, uint32_t value)
{
    return jelli_game_command(
        game,
        (JelliCommand){.kind = kind, .actor_id = game->pets[game->active].id, .value = value});
}

static void advance_ms(JelliGame *game, uint64_t milliseconds)
{
    for (; milliseconds >= 800u; milliseconds -= 800u)
        jelli_game_advance(game, 800u);
    jelli_game_advance(game, milliseconds);
}

static unsigned ninja_count(const JelliGame *game)
{
    unsigned count = 0u;
    for (unsigned i = 0u; i < game->count; ++i)
        count += game->pets[i].form == JELLI_FORM_NINJA ? 1u : 0u;
    return count;
}

/* A just-added ninja matches the one a new game starts with, apart from its ID. */
static bool fresh_ninja(const JelliPet *pet, uint32_t id)
{
    JelliGame reference;
    jelli_game_init(&reference);
    const JelliPet *start = &reference.pets[2];
    return pet->id == id && pet->form == JELLI_FORM_NINJA && pet->random_state == id &&
           pet->ticks == 0u && pet->stage_ticks == 0u && pet->bond == start->bond &&
           pet->bedtime == start->bedtime && pet->sleep_duration == start->sleep_duration &&
           pet->phase_offset == start->phase_offset && pet->health == JELLI_WELL &&
           pet->activity == JELLI_IDLE && !pet->asleep && pet->location == 0u &&
           memcmp(pet->needs, start->needs, sizeof(pet->needs)) == 0 &&
           memcmp(pet->need_remainders, start->need_remainders, sizeof(pet->need_remainders)) == 0;
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

/* The roster every build before the ninja wrote: Lilac and Mint, the Mint active. */
static JelliSave legacy_fixture(void)
{
    JelliSave save = {.sequence = 9u, .anchor_ms = 1700000000000u, .anchor_valid = true};
    jelli_game_init(&save.game);
    save.game.count = 2u;
    save.game.pets[2] = (JelliPet){0};
    save.game.pets[0].form = JELLI_FORM_LILAC;
    save.game.pets[0].bond = 420u;
    save.game.active = 1u;
    save.game.battery_display = false;
    return save;
}

enum { COUNT_OFFSET = 68u };
static const size_t PET_SIZE =
    130u + JELLI_HABIT_BIN_COUNT * 6u + 37u + JELLI_PRIZE_COUNT * 2u + 9u;

/* The current encoder only writes complete rosters, so a two-pet record is made by
 * encoding a placeholder third pet and cutting its record back out. */
static size_t encode_current(const JelliSave *save, uint8_t *bytes)
{
    static JelliSave padded;
    padded = *save;
    unsigned extra = 0u;
    if (padded.game.count < JELLI_ROSTER_MIN) {
        padded.game.pets[padded.game.count] = padded.game.pets[0];
        padded.game.pets[padded.game.count++].id = 200u;
        extra = 1u;
    }
    size_t size = jelli_save_encode(&padded, bytes, JELLI_SAVE_CAPACITY);
    CHECK(size > PREFIX + SETTING_FROM_END && bytes[4] == JELLI_SAVE_VERSION);
    size_t cut = PREFIX + (size_t)save->game.count * PET_SIZE;
    memmove(bytes + cut, bytes + cut + extra * PET_SIZE, size - cut - extra * PET_SIZE);
    bytes[COUNT_OFFSET] = save->game.count;
    return size - extra * PET_SIZE;
}

/* Rewrites the current layout as an older version. Version 5 kept the version 4
 * layout; 3 lacks the settings byte; 1 and 2 also lack the newer per-pet fields and
 * everything after the pets. */
static size_t encode_as(const JelliSave *save, unsigned version, uint8_t *out)
{
    static uint8_t current[JELLI_SAVE_CAPACITY];
    size_t size = encode_current(save, current);
    size_t length = size;
    if (version >= 4u) {
        memcpy(out, current, size);
    } else if (version == 3u) {
        length = size - 1u;
        memcpy(out, current, size - SETTING_FROM_END);
        memcpy(out + length - TRAILER, current + size - TRAILER, TRAILER);
    } else {
        size_t legacy_pet = version == 1u ? 112u : 130u;
        memcpy(out, current, PREFIX);
        length = PREFIX;
        for (unsigned i = 0u; i < save->game.count; ++i, length += legacy_pet)
            memcpy(out + length, current + PREFIX + i * PET_SIZE, legacy_pet);
        memcpy(out + length, current + size - TRAILER, TRAILER);
        length += TRAILER;
    }
    out[4] = (uint8_t)version;
    repair(out, length);
    return length;
}

static void new_game_has_three_pets(void)
{
    JelliGame game;
    jelli_game_init(&game);
    CHECK(jelli_game_valid(&game) && game.count == JELLI_ROSTER_MIN && game.active == 0u);
    CHECK(game.pets[0].id == 1u && game.pets[1].id == 2u && game.pets[2].id == JELLI_NINJA_PET_ID);
    CHECK(game.pets[0].form == JELLI_FORM_MINT && game.pets[1].form == JELLI_FORM_MINT);
    CHECK(fresh_ninja(&game.pets[2], JELLI_NINJA_PET_ID) && ninja_count(&game) == 1u);
    for (unsigned i = 0u; i < 2u; ++i) {
        const JelliPet *pet = &game.pets[i];
        CHECK(pet->bond == game.pets[2].bond && pet->bedtime == game.pets[2].bedtime);
        CHECK(memcmp(pet->needs, game.pets[2].needs, sizeof(pet->needs)) == 0);
    }
    game.count = 2u; /* Two pets is now an incomplete roster. */
    CHECK(!jelli_game_valid(&game));
}

static void form_validity(void)
{
    static const uint8_t forms[] = {0u, 1u, 2u, 3u, 4u, 127u, 255u};
    for (unsigned i = 0u; i < sizeof(forms); ++i) {
        JelliGame game;
        jelli_game_init(&game);
        game.pets[1].form = forms[i];
        CHECK(jelli_game_valid(&game) == (forms[i] < JELLI_FORM_COUNT));
    }
}

static void ninja_never_evolves(void)
{
    JelliGame ninja, mint;
    jelli_game_init(&ninja);
    ninja.active = 2u;
    jelli_game_init(&mint);
    advance_ms(&ninja, 600000u); /* Ten times the Mint's one-minute evolution. */
    advance_ms(&mint, 60000u);
    CHECK(mint.pets[0].form == JELLI_FORM_LILAC);
    CHECK(ninja.pets[2].form == JELLI_FORM_NINJA && ninja.pets[2].stage_ticks == 6000u);
    CHECK(ninja.pets[0].form == JELLI_FORM_MINT && ninja.pets[0].ticks == 0u);
    CHECK(jelli_game_valid(&ninja));
}

static void switch_cycles_in_roster_order(void)
{
    JelliGame game;
    jelli_game_init(&game);
    static const uint32_t order[] = {1u, 2u, 3u, 1u, 2u};
    CHECK(jelli_game_next_slot(NULL) == 0u);
    for (unsigned i = 0u; i + 1u < sizeof(order) / sizeof(order[0]); ++i) {
        CHECK(game.pets[game.active].id == order[i]);
        uint8_t next = jelli_game_next_slot(&game);
        CHECK(game.pets[next].id == order[i + 1u]);
        CHECK(command(&game, JELLI_CMD_ACTIVATE, game.pets[next].id) == JELLI_OK);
        CHECK(game.active == next && jelli_game_valid(&game));
    }
    /* The ninja cannot switch away while busy, resting under the sleep log, or resuming. */
    game.active = 2u;
    CHECK(command(&game, JELLI_CMD_FEED, 0u) == JELLI_OK);
    CHECK(command(&game, JELLI_CMD_ACTIVATE, 1u) == JELLI_NOT_READY && game.active == 2u);
    advance_ms(&game, 5000u);
    CHECK(game.pets[2].activity == JELLI_IDLE);
    CHECK(command(&game, JELLI_CMD_REST, 0u) == JELLI_OK && game.sleep_log.active);
    CHECK(command(&game, JELLI_CMD_ACTIVATE, 1u) == JELLI_NOT_READY && game.active == 2u);
    CHECK(command(&game, JELLI_CMD_WAKE, 0u) == JELLI_OK && !game.sleep_log.active);
    jelli_game_resume_begin(&game, 60000u);
    CHECK(command(&game, JELLI_CMD_ACTIVATE, 1u) != JELLI_OK && game.active == 2u);
    while (!jelli_game_resume_step(&game))
        CHECK(jelli_game_valid(&game));
    CHECK(command(&game, JELLI_CMD_ACTIVATE, 1u) == JELLI_OK && game.active == 0u);
}

static void add_ninja_rules(void)
{
    JelliSave legacy = legacy_fixture();
    JelliGame game = legacy.game;
    CHECK(!jelli_game_add_ninja(NULL));
    CHECK(jelli_game_add_ninja(&game) && game.count == 3u && game.active == 1u);
    CHECK(fresh_ninja(&game.pets[2], JELLI_NINJA_PET_ID) && jelli_game_valid(&game));
    CHECK(!jelli_game_add_ninja(&game) && game.count == 3u); /* Never a second ninja. */
    game = legacy.game;
    game.pets[1].id = JELLI_NINJA_PET_ID; /* ID 3 is taken: the next free ID is used. */
    game.pets[0].id = 4u;
    CHECK(jelli_game_add_ninja(&game) && fresh_ninja(&game.pets[2], 5u));
    game = legacy.game;
    game.count = JELLI_PET_CAPACITY;
    for (unsigned p = 2u; p < JELLI_PET_CAPACITY; ++p) {
        game.pets[p] = game.pets[0];
        game.pets[p].id = p + 1u;
    }
    CHECK(!jelli_game_add_ninja(&game) && game.count == JELLI_PET_CAPACITY);
    game = legacy.game;
    game.prizes.offered_pet = JELLI_NINJA_PET_ID; /* Unknown pet: no rescue by appending. */
    game.prizes.offered = 1u;
    CHECK(!jelli_game_add_ninja(&game) && game.count == 2u && game.pets[2].id == 0u);
}

static void add_ninja_untouched(JelliGame *game)
{
    static uint8_t before[sizeof(JelliGame)], after[sizeof(JelliGame)];
    memcpy(before, game, sizeof(before));
    CHECK(!jelli_game_add_ninja(game));
    memcpy(after, game, sizeof(after)); /* Raw bytes, padding included. */
    CHECK(memcmp(before, after, sizeof(before)) == 0);
}

/* Every refusal leaves the game byte-identical, padding included. */
static void add_ninja_refusals_untouched(void)
{
    const JelliGame legacy = legacy_fixture().game;
    JelliGame game;
    for (unsigned count = 0u; count < 2u; ++count) {
        game = legacy;
        game.count = (uint8_t)count;
        game.active = 0u;
        add_ninja_untouched(&game); /* Too few pets to be a roster. */
    }
    game = legacy;
    game.active = 2u;
    add_ninja_untouched(&game); /* Active slot past the roster. */
    game = legacy;
    game.pets[1].id = game.pets[0].id;
    add_ninja_untouched(&game); /* Duplicate IDs. */
    game = legacy;
    game.pets[5].id = 99u;
    add_ninja_untouched(&game); /* A stale record past the roster. */
    game = legacy;
    game.pets[0].form = JELLI_FORM_COUNT;
    add_ninja_untouched(&game); /* Unknown form. */
    for (unsigned at = 0u; at < 2u; ++at) {
        game = legacy;
        game.pets[at].form = JELLI_FORM_NINJA;
        add_ninja_untouched(&game); /* Two pets, one already the ninja. */
    }
    for (unsigned at = 0u; at <= JELLI_PET_CAPACITY; ++at) {
        game = legacy;
        game.count = JELLI_PET_CAPACITY;
        for (unsigned p = 2u; p < JELLI_PET_CAPACITY; ++p) {
            game.pets[p] = game.pets[1];
            game.pets[p].id = p + 1u;
        }
        if (at < JELLI_PET_CAPACITY)
            game.pets[at].form = JELLI_FORM_NINJA;
        add_ninja_untouched(&game); /* Full, with the ninja in any slot or none. */
    }
}

static void legacy_saves_gain_one_ninja(void)
{
    JelliSave legacy = legacy_fixture(), loaded, again;
    static uint8_t bytes[JELLI_SAVE_CAPACITY], resaved[JELLI_SAVE_CAPACITY];
    for (unsigned version = 1u; version <= 4u; ++version) {
        size_t size = encode_as(&legacy, version, bytes);
        CHECK(bytes[4] == version && jelli_save_decode(&loaded, bytes, size));
        CHECK(loaded.game.count == 3u && ninja_count(&loaded.game) == 1u);
        CHECK(fresh_ninja(&loaded.game.pets[2], JELLI_NINJA_PET_ID));
        CHECK(loaded.game.active == 1u && loaded.game.pets[0].form == JELLI_FORM_LILAC);
        CHECK(loaded.game.pets[0].bond == 420u && loaded.sequence == legacy.sequence);
        CHECK(loaded.game.battery_display == (version < 4u)); /* v4 kept the stored OFF. */
        size_t next = jelli_save_encode(&loaded, resaved, sizeof(resaved));
        CHECK(next != 0u && resaved[4] == JELLI_SAVE_VERSION);
        CHECK(jelli_save_decode(&again, resaved, next) && again.game.count == 3u);
        CHECK(ninja_count(&again.game) == 1u); /* Reloading v5 never adds another. */
    }
    /* A full legacy roster has no room; it loads unchanged rather than failing. */
    legacy.game.count = JELLI_PET_CAPACITY;
    for (unsigned p = 2u; p < JELLI_PET_CAPACITY; ++p) {
        legacy.game.pets[p] = legacy.game.pets[1];
        legacy.game.pets[p].id = p + 1u;
    }
    size_t size = encode_as(&legacy, 4u, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size) && loaded.game.count == JELLI_PET_CAPACITY);
    CHECK(ninja_count(&loaded.game) == 0u);
}

static void current_saves_are_not_migrated(void)
{
    JelliSave save = {.sequence = 3u}, loaded = {.sequence = 99u};
    static uint8_t bytes[JELLI_SAVE_CAPACITY], again[JELLI_SAVE_CAPACITY];
    jelli_game_init(&save.game);
    save.game.active = 2u;
    save.game.pets[2].bond = 777u;
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size != 0u && bytes[4] == 5u && JELLI_SAVE_VERSION == 5u);
    CHECK(jelli_save_decode(&loaded, bytes, size) && loaded.game.count == 3u);
    CHECK(loaded.game.active == 2u && loaded.game.pets[2].bond == 777u);
    CHECK(jelli_save_encode(&loaded, again, sizeof(again)) == size);
    CHECK(memcmp(bytes, again, size) == 0);
    /* A v4 file that somehow already holds a ninja is not given a second one. */
    size = encode_as(&save, 4u, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size) && ninja_count(&loaded.game) == 1u);
    /* A two-pet roster labelled v5 is incomplete, and newer versions are refused. */
    JelliSave legacy = legacy_fixture();
    size = encode_as(&legacy, 4u, bytes);
    bytes[4] = 5u;
    repair(bytes, size);
    loaded.sequence = 99u;
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == 99u);
    size = jelli_save_encode(&save, bytes, sizeof(bytes));
    bytes[4] = JELLI_SAVE_VERSION + 1u;
    repair(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == 99u);
}

static void invalid_legacy_roster_is_rejected(void)
{
    JelliSave legacy = legacy_fixture(), loaded = {.sequence = 55u};
    static uint8_t bytes[JELLI_SAVE_CAPACITY];
    legacy.game.prizes.discovered = 1u;
    legacy.game.prizes.origin_pet[0] = 1u;
    size_t size = encode_as(&legacy, 4u, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    /* Point the origin at pet 3, which the legacy roster lacks: corrupt before migration. */
    size_t origin = size - SETTING_FROM_END - 4u - 1u - (size_t)4u * JELLI_PRIZE_COUNT;
    CHECK(bytes[origin] == 1u);
    bytes[origin] = JELLI_NINJA_PET_ID;
    repair(bytes, size);
    loaded.sequence = 55u;
    CHECK(!jelli_save_decode(&loaded, bytes, size) && loaded.sequence == 55u);
}

/* A brand-new player starts with Nirmal the ninja; the roster itself is unchanged. */
static void new_player_starts_with_nirmal(void)
{
    JelliGame game, roster;
    jelli_game_new(&game);
    jelli_game_init(&roster);
    CHECK(jelli_game_valid(&game) && game.active == 2u);
    CHECK(game.pets[game.active].form == JELLI_FORM_NINJA &&
          game.pets[game.active].id == JELLI_NINJA_PET_ID);
    CHECK(roster.active == 0u && roster.count == game.count && roster.food == game.food &&
          roster.gifts == game.gifts && roster.battery_display == game.battery_display);
    for (uint8_t i = 0u; i < game.count; ++i)
        CHECK(roster.pets[i].id == game.pets[i].id && roster.pets[i].form == game.pets[i].form);
    CHECK(jelli_game_next_slot(&game) == 0u); /* SWITCH PET goes from Nirmal to the first Mint. */
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

int main(void)
{
    new_game_has_three_pets();
    new_player_starts_with_nirmal();
    form_validity();
    ninja_never_evolves();
    switch_cycles_in_roster_order();
    add_ninja_rules();
    add_ninja_refusals_untouched();
    legacy_saves_gain_one_ninja();
    current_saves_are_not_migrated();
    invalid_legacy_roster_is_rejected();
    puts("PASS: three-pet roster, ninja start/evolution/switching, v1-v4 to v5 migration");
    return 0;
}

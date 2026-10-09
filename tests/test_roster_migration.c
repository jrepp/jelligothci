/* Seeded property and mutation tests for the roster migrations. Every v1-v7 roster of
 * two to eight pets, with the active pet, any ninja, and any unicorn in each slot,
 * gains exactly the species its version predates (the ninja before v5, then the
 * unicorn before v7) when it has room and none yet; repeated load/save/load never adds
 * a second; a rejected record leaves the output untouched. Failures print the seed. */
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

/* Reports the generator seed so the failing roster can be rebuilt with build_case(). */
#define CHECK_SEED(expr, seed)                                                                     \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x)\n", __FILE__, __LINE__, #expr,                \
                    (unsigned)(seed));                                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* Version 4 added the battery setting and version 6 the sound level after it. */
enum { TRAILER = 8u, PREFIX = 71u, COUNT_OFFSET = 68u, SETTING_FROM_END = TRAILER + 2u };
/* Versions before 5 predate the ninja and versions before 7 the unicorn. */
enum { NINJA_VERSION = 5u, UNICORN_VERSION = 7u };
enum { NONE = JELLI_PET_CAPACITY, ID_POOL = 12u, CYCLES = 3u };
static const size_t PET_SIZE =
    130u + JELLI_HABIT_BIN_COUNT * 6u + 37u + JELLI_PRIZE_COUNT * 2u + 9u;

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
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

static unsigned form_count(const JelliGame *game, uint8_t form)
{
    unsigned count = 0u;
    for (unsigned i = 0u; i < game->count && i < JELLI_PET_CAPACITY; ++i)
        count += game->pets[i].form == form ? 1u : 0u;
    return count;
}

/* Rewrites any roster as an older version. Shorter rosters borrow placeholder records
 * from the encoder that are cut back out. Versions 6 and 7 share a layout; 4 and 5
 * lack the v6 sound level, 3 also the battery setting, and 1 and 2 also the newer
 * per-pet fields and everything after the pets. */
static size_t encode_as(const JelliSave *save, unsigned version, uint8_t *out)
{
    static JelliSave padded;
    static uint8_t current[JELLI_SAVE_CAPACITY];
    padded = *save;
    size_t extra = 0u;
    for (; padded.game.count < JELLI_ROSTER_MIN; extra += PET_SIZE) {
        padded.game.pets[padded.game.count] = padded.game.pets[0];
        padded.game.pets[padded.game.count++].id = 200u + (uint32_t)(extra / PET_SIZE);
    }
    size_t size = jelli_save_encode(&padded, current, JELLI_SAVE_CAPACITY);
    CHECK(size > PREFIX + SETTING_FROM_END + extra && current[4] == JELLI_SAVE_VERSION);
    size_t cut = PREFIX + (size_t)save->game.count * PET_SIZE;
    memmove(current + cut, current + cut + extra, size - cut - extra);
    current[COUNT_OFFSET] = save->game.count;
    size -= extra;
    size_t length = size;
    if (version >= 6u) {
        memcpy(out, current, size);
    } else if (version >= 3u) {
        size_t dropped = version == 3u ? 2u : 1u;
        length = size - dropped;
        memcpy(out, current, size - TRAILER - dropped);
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

/* Decodes through both entry points into sentinel-filled outputs; a rejection must
 * not write one output byte, an acceptance must agree across the two APIs. */
static bool decode_checked(const uint8_t *bytes, size_t size, JelliSave *out)
{
    static JelliSave first, second, scratch;
    static uint8_t before[sizeof(JelliSave)];
    memset(&first, 0xa5, sizeof(first));
    memset(&second, 0x5a, sizeof(second));
    memcpy(before, &first, sizeof(before));
    bool accepted = jelli_save_decode_workspace(&first, bytes, size, &scratch);
    CHECK(accepted == jelli_save_decode(&second, bytes, size));
    if (!accepted) {
        static uint8_t after[sizeof(JelliSave)];
        memcpy(after, &first, sizeof(after)); /* Raw bytes, padding included. */
        CHECK(memcmp(before, after, sizeof(before)) == 0);
        return false;
    }
    CHECK(first.game.count == second.game.count && first.sequence == second.sequence);
    if (out)
        *out = first;
    return true;
}

typedef struct {
    unsigned version, count, active, ninja_at, unicorn_at;
} Shape;

/* A stored roster: distinct IDs drawn from 1..12 (so IDs 3 and 4 are often jellies),
 * varied stats, sleeping bystanders, and on v3+ a prize whose origin is one of the pets. */
static JelliSave build_case(const Shape *shape, uint32_t seed)
{
    JelliSave save = {.sequence = next_random(&seed), .anchor_ms = next_random(&seed)};
    save.anchor_valid = (next_random(&seed) & 1u) != 0u;
    jelli_game_init(&save.game);
    const JelliPet base = save.game.pets[0];
    uint32_t pool[ID_POOL];
    for (unsigned i = 0u; i < ID_POOL; ++i)
        pool[i] = i + 1u;
    for (unsigned i = ID_POOL - 1u; i > 0u; --i) {
        unsigned j = next_random(&seed) % (i + 1u);
        uint32_t swap = pool[i];
        pool[i] = pool[j];
        pool[j] = swap;
    }
    for (unsigned p = 0u; p < JELLI_PET_CAPACITY; ++p) {
        JelliPet *pet = &save.game.pets[p];
        *pet = p < shape->count ? base : (JelliPet){0};
        if (p >= shape->count)
            continue;
        pet->id = pool[p];
        pet->random_state = next_random(&seed) | 1u;
        pet->form = p == shape->ninja_at              ? (uint8_t)JELLI_FORM_NINJA
                    : p == shape->unicorn_at          ? (uint8_t)JELLI_FORM_UNICORN
                    : (next_random(&seed) & 1u) != 0u ? (uint8_t)JELLI_FORM_LILAC
                                                      : (uint8_t)JELLI_FORM_MINT;
        pet->bond = (uint16_t)(next_random(&seed) % 1001u);
        pet->location = (uint8_t)(next_random(&seed) & 1u);
        pet->bedtime = next_random(&seed) % 24u;
        pet->ticks = next_random(&seed) % 2000000u;
        pet->stage_ticks = next_random(&seed) % (pet->form == JELLI_FORM_MINT ? 600u : 9000000u);
        for (unsigned n = 0u; n < JELLI_NEED_COUNT; ++n)
            pet->needs[n] = (uint16_t)(next_random(&seed) % 1001u);
        pet->asleep = pet->scheduled_sleep = p != shape->active && next_random(&seed) % 4u == 0u;
    }
    save.game.count = (uint8_t)shape->count;
    save.game.active = (uint8_t)shape->active;
    save.game.food = (uint16_t)(next_random(&seed) % (JELLI_STACK_LIMIT + 1u));
    save.game.gifts = (uint16_t)(next_random(&seed) % (JELLI_STACK_LIMIT + 1u));
    save.game.battery_display = shape->version < 4u || (next_random(&seed) & 1u) != 0u;
    if (shape->version >= 3u && (next_random(&seed) & 1u) != 0u) {
        save.game.prizes.discovered = 1u;
        save.game.prizes.origin_pet[0] = save.game.pets[next_random(&seed) % shape->count].id;
    }
    save.game.sound_level = (uint8_t)(next_random(&seed) % (JELLI_SOUND_LEVEL_MAX + 1u));
    return save;
}

/* The oracle: the stored roster with each predated, absent species appended in order
 * (while there is room), taking the first unused ID from the species' stable ID. */
static JelliGame expected_roster(const JelliGame *stored, unsigned version)
{
    JelliGame game = *stored, reference;
    jelli_game_init(&reference);
    static const uint8_t forms[] = {JELLI_FORM_NINJA, JELLI_FORM_UNICORN};
    static const unsigned before[] = {NINJA_VERSION, UNICORN_VERSION};
    for (unsigned s = 0u; s < 2u; ++s) {
        if (version >= before[s] || form_count(&game, forms[s]) || game.count >= JELLI_PET_CAPACITY)
            continue;
        JelliPet pet = reference.pets[2u + s];
        for (bool used = true; used;) {
            used = false;
            for (unsigned p = 0u; p < game.count; ++p)
                used = used || game.pets[p].id == pet.id;
            pet.id += used ? 1u : 0u;
        }
        pet.random_state = pet.id;
        game.pets[game.count++] = pet;
    }
    return game;
}

static bool same_pet(const JelliPet *a, const JelliPet *b)
{
    return a->id == b->id && a->form == b->form && a->bond == b->bond &&
           a->location == b->location && a->bedtime == b->bedtime && a->ticks == b->ticks &&
           a->stage_ticks == b->stage_ticks && a->asleep == b->asleep &&
           a->scheduled_sleep == b->scheduled_sleep && a->nap_due == b->nap_due &&
           a->health == b->health && a->activity == b->activity &&
           a->random_state == b->random_state && memcmp(a->needs, b->needs, sizeof(a->needs)) == 0;
}

/* Save, reload, and save again: the roster never changes and the re-encoded bytes are
 * a fixed point from the first save onwards. */
static void cycle_is_stable(const JelliSave *loaded, uint32_t seed)
{
    static uint8_t first[JELLI_SAVE_CAPACITY], again[JELLI_SAVE_CAPACITY];
    JelliSave current = *loaded;
    size_t size = jelli_save_encode(&current, first, sizeof(first));
    CHECK_SEED(size != 0u && first[4] == JELLI_SAVE_VERSION, seed);
    for (unsigned i = 0u; i < CYCLES; ++i) {
        JelliSave next;
        CHECK_SEED(decode_checked(first, size, &next), seed);
        CHECK_SEED(next.game.count == loaded->game.count, seed);
        for (uint8_t form = 0u; form < JELLI_FORM_COUNT; ++form)
            CHECK_SEED(form_count(&next.game, form) == form_count(&loaded->game, form), seed);
        CHECK_SEED(jelli_save_encode(&next, again, sizeof(again)) == size, seed);
        CHECK_SEED(memcmp(first, again, size) == 0, seed);
        current = next;
    }
}

static void check_accepted(const Shape *shape, const JelliSave *stored, const JelliGame *expected,
                           const JelliSave *loaded, uint32_t seed)
{
    CHECK_SEED(loaded->game.count == expected->count, seed);
    CHECK_SEED(loaded->game.active == shape->active && jelli_game_valid(&loaded->game), seed);
    CHECK_SEED(loaded->sequence == stored->sequence && loaded->anchor_ms == stored->anchor_ms,
               seed);
    CHECK_SEED(loaded->game.food == stored->game.food, seed);
    CHECK_SEED(loaded->game.battery_display == stored->game.battery_display, seed);
    CHECK_SEED(loaded->game.sound_level ==
                   (shape->version >= 6u ? stored->game.sound_level : JELLI_SOUND_LEVEL_DEFAULT),
               seed);
    for (unsigned p = 0u; p < expected->count; ++p)
        CHECK_SEED(same_pet(&loaded->game.pets[p], &expected->pets[p]), seed);
    if (shape->version >= 3u)
        CHECK_SEED(loaded->game.prizes.origin_pet[0] == stored->game.prizes.origin_pet[0], seed);
    for (uint8_t form = JELLI_FORM_NINJA; form < JELLI_FORM_COUNT; ++form)
        CHECK_SEED(form_count(&loaded->game, form) <= 1u, seed); /* Never a duplicate. */
    for (unsigned p = loaded->game.count; p < JELLI_PET_CAPACITY; ++p)
        CHECK_SEED(loaded->game.pets[p].id == 0u, seed);
    cycle_is_stable(loaded, seed);
}

typedef struct {
    unsigned cases, ninjas, unicorns, moved, full, rejected;
} Tally;

static void run_case(const Shape *shape, uint32_t seed, Tally *tally)
{
    static uint8_t bytes[JELLI_SAVE_CAPACITY];
    JelliSave stored = build_case(shape, seed), loaded;
    JelliGame expected = expected_roster(&stored.game, shape->version);
    size_t size = encode_as(&stored, shape->version, bytes);
    CHECK_SEED(bytes[4] == shape->version, seed);
    bool accepted = decode_checked(bytes, size, &loaded);
    /* Only a roster still short of four pets after migration is refused. */
    CHECK_SEED(accepted == (expected.count >= JELLI_ROSTER_MIN), seed);
    if (accepted)
        check_accepted(shape, &stored, &expected, &loaded, seed);
    tally->cases++;
    tally->rejected += accepted ? 0u : 1u;
    tally->full += shape->count == JELLI_PET_CAPACITY && shape->version < UNICORN_VERSION ? 1u : 0u;
    for (unsigned p = shape->count; p < expected.count; ++p) {
        bool ninja = expected.pets[p].form == JELLI_FORM_NINJA;
        tally->ninjas += ninja ? 1u : 0u;
        tally->unicorns += ninja ? 0u : 1u;
        tally->moved +=
            expected.pets[p].id != (ninja ? JELLI_NINJA_PET_ID : JELLI_UNICORN_PET_ID) ? 1u : 0u;
    }
}

/* Every version x count x active slot x ninja slot x unicorn slot (or none). */
static void every_roster_shape(void)
{
    Tally tally = {0};
    for (unsigned version = 1u; version <= JELLI_SAVE_VERSION; ++version)
        for (unsigned count = 2u; count <= JELLI_PET_CAPACITY; ++count)
            for (unsigned active = 0u; active < count; ++active)
                for (unsigned ninja = 0u; ninja <= count; ++ninja)
                    for (unsigned unicorn = 0u; unicorn <= count; ++unicorn) {
                        if (ninja == unicorn && ninja < count)
                            continue; /* One pet, one form. */
                        Shape shape = {version, count, active, ninja == count ? NONE : ninja,
                                       unicorn == count ? NONE : unicorn};
                        uint32_t seed = (version << 24u) ^ (count << 16u) ^ (active << 12u) ^
                                        (ninja << 4u) ^ unicorn;
                        run_case(&shape, (seed * 2654435761u) | 1u, &tally);
                    }
    CHECK(tally.cases == 7u * 1533u && tally.ninjas > 0u && tally.unicorns > tally.ninjas);
    CHECK(tally.rejected > 0u && tally.moved > 0u && tally.full > 0u);
    printf("roster shapes: %u cases, %u gained a ninja, %u a unicorn (%u not at their stable "
           "ID), %u full, %u refused\n",
           tally.cases, tally.ninjas, tally.unicorns, tally.moved, tally.full, tally.rejected);
}

/* IDs from 3 upwards already taken by jellies: each species takes the first gap above
 * its stable ID, the ninja first; a seven-pet roster has room only for the ninja. */
static void species_take_first_free_id(void)
{
    static const struct {
        uint32_t ids[7];
        unsigned count;
        uint32_t ninja, unicorn;
    } rosters[] = {
        {{3u, 4u, 5u, 6u, 7u, 8u, 9u}, 7u, 10u, 0u}, {{1u, 2u, 3u, 4u, 5u, 6u, 7u}, 7u, 8u, 0u},
        {{9u, 3u, 1u, 5u, 4u, 2u, 6u}, 7u, 7u, 0u},  {{3u, 4u, 5u, 6u, 7u, 8u}, 6u, 9u, 10u},
        {{1u, 2u, 4u, 5u, 9u, 10u}, 6u, 3u, 6u},     {{1u, 2u, 3u, 5u, 6u, 7u}, 6u, 4u, 8u}};
    static uint8_t bytes[JELLI_SAVE_CAPACITY];
    for (unsigned r = 0u; r < sizeof(rosters) / sizeof(rosters[0]); ++r) {
        Shape shape = {4u, rosters[r].count, rosters[r].count - 1u, NONE, NONE};
        JelliSave stored = build_case(&shape, 0x1d000000u + r), loaded;
        for (unsigned p = 0u; p < rosters[r].count; ++p)
            stored.game.pets[p].id = rosters[r].ids[p];
        stored.game.prizes = (JelliPrizes){0};
        size_t size = encode_as(&stored, 4u, bytes);
        CHECK(decode_checked(bytes, size, &loaded) &&
              loaded.game.count == rosters[r].count + 1u + (rosters[r].unicorn != 0u));
        CHECK(loaded.game.pets[rosters[r].count].id == rosters[r].ninja);
        CHECK(loaded.game.pets[rosters[r].count].form == JELLI_FORM_NINJA);
        if (rosters[r].unicorn)
            CHECK(loaded.game.pets[rosters[r].count + 1u].id == rosters[r].unicorn &&
                  loaded.game.pets[rosters[r].count + 1u].form == JELLI_FORM_UNICORN);
    }
}

/* Mutated older records never crash; anything accepted is a valid roster that has
 * already converged, so saving and reloading it never adds another pet. */
static void legacy_mutation_fuzz(void)
{
    static uint8_t bytes[JELLI_SAVE_CAPACITY];
    const uint32_t base = UINT32_C(0x1e6ac7f5);
    unsigned accepted = 0u, total = 0u;
    for (unsigned i = 0u; i < 3000u; ++i) {
        uint32_t seed = base + i, state = seed;
        unsigned count = 2u + next_random(&state) % (JELLI_PET_CAPACITY - 1u);
        Shape shape = {1u + next_random(&state) % (UNICORN_VERSION - 1u), count,
                       next_random(&state) % count, next_random(&state) % (count + 1u), NONE};
        if (shape.ninja_at == count)
            shape.ninja_at = NONE;
        JelliSave stored = build_case(&shape, seed), loaded;
        size_t size = encode_as(&stored, shape.version, bytes);
        for (unsigned r = 1u + next_random(&state) % 3u; r > 0u; --r) {
            size_t at = COUNT_OFFSET + next_random(&state) % (size - TRAILER - COUNT_OFFSET);
            bytes[at] = (next_random(&state) & 1u) != 0u
                            ? (uint8_t)(bytes[at] ^ (1u << (next_random(&state) % 8u)))
                            : (uint8_t)next_random(&state);
        }
        repair(bytes, size);
        if (decode_checked(bytes, size, &loaded)) {
            CHECK_SEED(jelli_game_valid(&loaded.game), seed);
            cycle_is_stable(&loaded, seed);
            ++accepted;
        }
        ++total;
    }
    CHECK(accepted > 0u && accepted < total);
    printf("legacy mutation fuzz: %u of %u mutants loaded and converged\n", accepted, total);
}

int main(void)
{
    every_roster_shape();
    species_take_first_free_id();
    legacy_mutation_fuzz();
    puts("PASS: v1-v6 roster migration properties and seeded mutation fuzz");
    return 0;
}

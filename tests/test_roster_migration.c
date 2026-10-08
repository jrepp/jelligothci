/* Seeded property and mutation tests for the version 5 roster migration. Every v1-v4
 * roster of two to eight pets, with the active pet and any ninja in each slot, gains
 * exactly one ninja when it has room and none yet; repeated load/save/load never adds
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

enum { TRAILER = 8u, PREFIX = 71u, COUNT_OFFSET = 68u, SETTING_FROM_END = TRAILER + 1u };
enum { NO_NINJA = JELLI_PET_CAPACITY, ID_POOL = 12u, CYCLES = 3u };
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

static unsigned ninja_count(const JelliGame *game)
{
    unsigned count = 0u;
    for (unsigned i = 0u; i < game->count && i < JELLI_PET_CAPACITY; ++i)
        count += game->pets[i].form == JELLI_FORM_NINJA ? 1u : 0u;
    return count;
}

/* Encodes any roster in the current layout; a two-pet roster borrows a placeholder
 * third record from the encoder and has it cut back out. */
static size_t encode_layout(const JelliSave *save, uint8_t *bytes)
{
    static JelliSave padded;
    padded = *save;
    size_t extra = 0u;
    if (padded.game.count < JELLI_ROSTER_MIN) {
        padded.game.pets[padded.game.count] = padded.game.pets[0];
        padded.game.pets[padded.game.count++].id = 200u;
        extra = PET_SIZE;
    }
    size_t size = jelli_save_encode(&padded, bytes, JELLI_SAVE_CAPACITY);
    CHECK(size > PREFIX + SETTING_FROM_END + extra && bytes[4] == JELLI_SAVE_VERSION);
    size_t cut = PREFIX + (size_t)save->game.count * PET_SIZE;
    memmove(bytes + cut, bytes + cut + extra, size - cut - extra);
    bytes[COUNT_OFFSET] = save->game.count;
    return size - extra;
}

/* Rewrites as an older version: 4 and 5 share a layout, 3 lacks the settings byte,
 * and 1 and 2 lack the newer per-pet fields and everything after the pets. */
static size_t encode_as(const JelliSave *save, unsigned version, uint8_t *out)
{
    static uint8_t current[JELLI_SAVE_CAPACITY];
    size_t size = encode_layout(save, current), length = size;
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
    unsigned version, count, active, ninja_at;
} Shape;

/* A legacy roster: distinct IDs drawn from 1..12 (so ID 3 is often a jelly), varied
 * stats, sleeping bystanders, and on v3+ a prize whose origin is one of the pets. */
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
    return save;
}

static uint32_t expected_ninja_id(const JelliGame *game)
{
    for (uint32_t id = JELLI_NINJA_PET_ID;; ++id) {
        bool used = false;
        for (unsigned p = 0u; p < game->count; ++p)
            used = used || game->pets[p].id == id;
        if (!used)
            return id;
    }
}

static bool same_pet(const JelliPet *a, const JelliPet *b)
{
    return a->id == b->id && a->form == b->form && a->bond == b->bond &&
           a->location == b->location && a->bedtime == b->bedtime && a->ticks == b->ticks &&
           a->stage_ticks == b->stage_ticks && a->asleep == b->asleep &&
           a->random_state == b->random_state && memcmp(a->needs, b->needs, sizeof(a->needs)) == 0;
}

/* The appended ninja is a fresh one, identical to a new game's apart from its ID. */
static bool fresh_ninja(const JelliPet *pet, uint32_t id)
{
    JelliGame reference;
    jelli_game_init(&reference);
    JelliPet expected = reference.pets[2];
    expected.id = expected.random_state = id;
    return same_pet(pet, &expected) && pet->health == JELLI_WELL && pet->activity == JELLI_IDLE &&
           !pet->scheduled_sleep && pet->nap_due == 0u;
}

/* Save, reload, and save again: the count and ninja total never change and the
 * re-encoded bytes are a fixed point from the first save onwards. */
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
        CHECK_SEED(ninja_count(&next.game) == ninja_count(&loaded->game), seed);
        CHECK_SEED(jelli_save_encode(&next, again, sizeof(again)) == size, seed);
        CHECK_SEED(memcmp(first, again, size) == 0, seed);
        current = next;
    }
}

static void check_accepted(const Shape *shape, const JelliSave *legacy, const JelliSave *loaded,
                           uint32_t seed)
{
    bool grows = shape->ninja_at == NO_NINJA && shape->count < JELLI_PET_CAPACITY;
    CHECK_SEED(loaded->game.count == shape->count + (grows ? 1u : 0u), seed);
    CHECK_SEED(loaded->game.active == shape->active && jelli_game_valid(&loaded->game), seed);
    CHECK_SEED(loaded->sequence == legacy->sequence && loaded->anchor_ms == legacy->anchor_ms,
               seed);
    CHECK_SEED(loaded->game.food == legacy->game.food, seed);
    CHECK_SEED(loaded->game.battery_display == legacy->game.battery_display, seed);
    for (unsigned p = 0u; p < shape->count; ++p)
        CHECK_SEED(same_pet(&loaded->game.pets[p], &legacy->game.pets[p]), seed);
    if (shape->version >= 3u)
        CHECK_SEED(loaded->game.prizes.origin_pet[0] == legacy->game.prizes.origin_pet[0], seed);
    CHECK_SEED(ninja_count(&loaded->game) == (shape->ninja_at == NO_NINJA ? 0u : 1u) + grows, seed);
    if (grows)
        CHECK_SEED(fresh_ninja(&loaded->game.pets[shape->count], expected_ninja_id(&legacy->game)),
                   seed);
    for (unsigned p = loaded->game.count; p < JELLI_PET_CAPACITY; ++p)
        CHECK_SEED(loaded->game.pets[p].id == 0u, seed);
    cycle_is_stable(loaded, seed);
}

typedef struct {
    unsigned cases, grown, full, rejected, id3_taken;
} Tally;

static void run_case(const Shape *shape, uint32_t seed, Tally *tally)
{
    static uint8_t bytes[JELLI_SAVE_CAPACITY];
    JelliSave legacy = build_case(shape, seed), loaded;
    bool has_ninja = shape->ninja_at != NO_NINJA;
    bool grows = !has_ninja && shape->count < JELLI_PET_CAPACITY;
    size_t size = encode_as(&legacy, shape->version, bytes);
    CHECK_SEED(bytes[4] == shape->version, seed);
    bool accepted = decode_checked(bytes, size, &loaded);
    /* Only a two-pet roster that already has a ninja stays incomplete. */
    CHECK_SEED(accepted == (shape->count + (grows ? 1u : 0u) >= JELLI_ROSTER_MIN), seed);
    if (accepted)
        check_accepted(shape, &legacy, &loaded, seed);
    tally->cases++;
    tally->rejected += accepted ? 0u : 1u;
    tally->grown += grows ? 1u : 0u;
    tally->full += !has_ninja && !grows ? 1u : 0u;
    tally->id3_taken += grows && expected_ninja_id(&legacy.game) != JELLI_NINJA_PET_ID ? 1u : 0u;
    /* The same record labelled version 5 is never migrated: it is complete or refused. */
    size = encode_as(&legacy, 4u, bytes);
    bytes[4] = (uint8_t)JELLI_SAVE_VERSION;
    repair(bytes, size);
    accepted = decode_checked(bytes, size, &loaded);
    CHECK_SEED(accepted == (shape->count >= JELLI_ROSTER_MIN), seed);
    if (accepted)
        CHECK_SEED(loaded.game.count == shape->count &&
                       ninja_count(&loaded.game) == (has_ninja ? 1u : 0u),
                   seed);
}

/* Every version x count x active slot x ninja slot (or none), each with two seeds. */
static void every_roster_shape(void)
{
    Tally tally = {0};
    for (unsigned version = 1u; version < JELLI_SAVE_VERSION; ++version) {
        for (unsigned count = 2u; count <= JELLI_PET_CAPACITY; ++count) {
            for (unsigned active = 0u; active < count; ++active) {
                for (unsigned ninja = 0u; ninja <= count; ++ninja) {
                    Shape shape = {version, count, active, ninja == count ? NO_NINJA : ninja};
                    uint32_t seed = (version << 24u) ^ (count << 16u) ^ (active << 8u) ^ ninja;
                    run_case(&shape, seed | 0x80000000u, &tally);
                    run_case(&shape, (seed * 2654435761u) | 1u, &tally);
                }
            }
        }
    }
    CHECK(tally.cases == 4u * 2u * 238u && tally.grown > 0u && tally.full > 0u);
    CHECK(tally.rejected > 0u && tally.id3_taken > 0u);
    printf("roster shapes: %u cases, %u gained a ninja (%u not as ID 3), %u full, %u refused\n",
           tally.cases, tally.grown, tally.id3_taken, tally.full, tally.rejected);
}

/* IDs 3 upwards already taken by jellies: the ninja takes the first gap. */
static void ninja_takes_first_free_id(void)
{
    static const uint32_t rosters[][7] = {
        {3u, 4u, 5u, 6u, 7u, 8u, 9u}, {1u, 2u, 3u, 4u, 5u, 6u, 7u}, {9u, 3u, 1u, 5u, 4u, 2u, 6u}};
    static const uint32_t expected[] = {10u, 8u, 7u};
    static uint8_t bytes[JELLI_SAVE_CAPACITY];
    for (unsigned r = 0u; r < sizeof(expected) / sizeof(expected[0]); ++r) {
        Shape shape = {4u, 7u, 6u, NO_NINJA};
        JelliSave legacy = build_case(&shape, 0x1d000000u + r), loaded;
        for (unsigned p = 0u; p < 7u; ++p)
            legacy.game.pets[p].id = rosters[r][p];
        legacy.game.prizes = (JelliPrizes){0};
        size_t size = encode_as(&legacy, 4u, bytes);
        CHECK(decode_checked(bytes, size, &loaded) && loaded.game.count == 8u);
        CHECK(fresh_ninja(&loaded.game.pets[7], expected[r]));
    }
}

/* Mutated legacy records never crash; anything accepted is a valid roster that has
 * already converged, so saving and reloading it never adds another pet. */
static void legacy_mutation_fuzz(void)
{
    static uint8_t bytes[JELLI_SAVE_CAPACITY];
    const uint32_t base = UINT32_C(0x1e6ac7f5);
    unsigned accepted = 0u, total = 0u;
    for (unsigned i = 0u; i < 3000u; ++i) {
        uint32_t seed = base + i, state = seed;
        unsigned count = 2u + next_random(&state) % (JELLI_PET_CAPACITY - 1u);
        Shape shape = {1u + next_random(&state) % 4u, count, next_random(&state) % count,
                       next_random(&state) % (count + 1u)};
        if (shape.ninja_at == count)
            shape.ninja_at = NO_NINJA;
        JelliSave legacy = build_case(&shape, seed), loaded;
        size_t size = encode_as(&legacy, shape.version, bytes);
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
    ninja_takes_first_free_id();
    legacy_mutation_fuzz();
    puts("PASS: v1-v4 roster migration properties and seeded mutation fuzz");
    return 0;
}

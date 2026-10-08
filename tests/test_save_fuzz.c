/* Property, mutation, and fuzz tests for the version 6 save codec.
 * Every generated case uses a fixed seed so a failure prints a replayable case. */
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

/* Reports the generator seed and iteration so the failing input can be replayed. */
#define CHECK_CASE(expr, seed, iteration)                                                          \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed %u, iteration %u)\n", __FILE__, __LINE__, #expr,      \
                    (unsigned)(seed), (unsigned)(iteration));                                      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* The battery setting (v4) and the sound level (v6) are the last record bytes. */
enum {
    TRAILER = 8u,
    HEADER = 32u,
    SOUND_FROM_END = TRAILER + 1u,
    SETTING_FROM_END = TRAILER + 2u,
    MUTATIONS = 6000u
};

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Recomputes the declared size and CRC so mutations reach the field decoder. */
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

static JelliSave fixture(bool shown, uint8_t level)
{
    JelliSave save = {.sequence = 77u, .anchor_ms = 1700000000000u, .anchor_valid = true};
    jelli_game_init(&save.game);
    save.game.timezone_minutes = -330;
    save.game.clock_adjust = 25;
    save.game.prizes.owned = 1u;
    save.game.prizes.discovered = 3u;
    save.game.prizes.origin_pet[0] = 1u; /* Every discovered prize records its origin. */
    save.game.prizes.origin_pet[1] = 2u;
    save.game.prizes.offered = 2u;
    save.game.prizes.offered_pet = 2u;
    save.game.pets[1].reward_pending = true;
    save.game.battery_display = shown;
    save.game.sound_level = level;
    return save;
}

static size_t encode(const JelliSave *save, uint8_t *bytes)
{
    size_t size = jelli_save_encode(save, bytes, JELLI_SAVE_CAPACITY);
    CHECK(size > HEADER + SETTING_FROM_END);
    return size;
}

/* Decodes through both entry points into sentinel-filled outputs. A rejection
 * must not write a single output byte; an acceptance must agree across APIs. */
static bool decode_checked(const uint8_t *bytes, size_t size, JelliSave *out)
{
    static JelliSave first, second, scratch;
    static uint8_t before[sizeof(JelliSave)];
    memset(&first, 0xa5, sizeof(first));
    memset(&second, 0x5a, sizeof(second));
    memcpy(before, &first, sizeof(before));
    bool accepted = jelli_save_decode_workspace(&first, bytes, size, &scratch);
    bool again = jelli_save_decode(&second, bytes, size);
    CHECK(accepted == again); /* Deterministic, and independent of the scratch storage. */
    if (!accepted) {
        static uint8_t after[sizeof(JelliSave)];
        memcpy(after, &first, sizeof(after)); /* Raw bytes, padding included. */
        CHECK(memcmp(before, after, sizeof(before)) == 0);
        return false;
    }
    uint8_t a[JELLI_SAVE_CAPACITY], b[JELLI_SAVE_CAPACITY];
    size_t size_a = encode(&first, a), size_b = encode(&second, b);
    CHECK(size_a == size_b && memcmp(a, b, size_a) == 0);
    if (out)
        *out = first;
    return true;
}

/* Re-encoding an accepted record; true when it reproduces the input exactly. */
static bool reencodes_exactly(const uint8_t *bytes, size_t size, uint8_t *again, size_t *again_size)
{
    JelliSave decoded;
    if (!decode_checked(bytes, size, &decoded))
        return false;
    *again_size = encode(&decoded, again);
    return *again_size == size && memcmp(again, bytes, size) == 0;
}

/* Any accepted current-version record is canonical: re-encoding reproduces it exactly.
 * An accepted older record migrates in one step: its current re-encoding is canonical. */
static bool canonical(const uint8_t *bytes, size_t size)
{
    uint8_t again[JELLI_SAVE_CAPACITY], twice[JELLI_SAVE_CAPACITY];
    size_t again_size = 0u, twice_size = 0u;
    bool exact = reencodes_exactly(bytes, size, again, &again_size);
    if (bytes[4] == JELLI_SAVE_VERSION || again_size == 0u)
        return exact;
    return again[4] == JELLI_SAVE_VERSION &&
           reencodes_exactly(again, again_size, twice, &twice_size);
}

static void round_trip_property(void)
{
    uint32_t seed = UINT32_C(0x5eed0004);
    for (unsigned i = 0; i < 64u; ++i) {
        JelliSave save = fixture((next_random(&seed) & 1u) != 0u,
                                 (uint8_t)(next_random(&seed) % (JELLI_SOUND_LEVEL_MAX + 1u))),
                  loaded;
        save.sequence = next_random(&seed);
        save.anchor_valid = (next_random(&seed) & 1u) != 0u;
        save.game.count =
            (uint8_t)(JELLI_ROSTER_MIN +
                      next_random(&seed) % (JELLI_PET_CAPACITY - JELLI_ROSTER_MIN + 1u));
        for (unsigned p = JELLI_ROSTER_MIN; p < save.game.count; ++p) {
            save.game.pets[p] = save.game.pets[p % JELLI_ROSTER_MIN];
            save.game.pets[p].id = p + 1u;
        }
        uint8_t bytes[JELLI_SAVE_CAPACITY];
        size_t size = encode(&save, bytes);
        CHECK_CASE(bytes[4] == JELLI_SAVE_VERSION, UINT32_C(0x5eed0004), i);
        CHECK_CASE(bytes[size - SETTING_FROM_END] == (save.game.battery_display ? 1u : 0u),
                   UINT32_C(0x5eed0004), i);
        CHECK_CASE(decode_checked(bytes, size, &loaded) && canonical(bytes, size),
                   UINT32_C(0x5eed0004), i);
        CHECK_CASE(bytes[size - SOUND_FROM_END] == save.game.sound_level, UINT32_C(0x5eed0004), i);
        CHECK_CASE(loaded.game.battery_display == save.game.battery_display, UINT32_C(0x5eed0004),
                   i);
        CHECK_CASE(loaded.game.sound_level == save.game.sound_level, UINT32_C(0x5eed0004), i);
    }
}

static void every_truncation_and_extension(void)
{
    JelliSave save = fixture(false, 0u), loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY], cut[JELLI_SAVE_CAPACITY];
    size_t size = encode(&save, bytes);
    for (size_t length = 0; length < size; ++length) {
        CHECK(!decode_checked(bytes, length, NULL)); /* Raw prefix: declared size mismatch. */
        if (length < HEADER + TRAILER)
            continue;
        /* Body truncated but framing repaired: the field decoder must notice. */
        memcpy(cut, bytes, length - TRAILER);
        memcpy(cut + length - TRAILER, bytes + size - TRAILER, TRAILER);
        repair(cut, length);
        CHECK(!decode_checked(cut, length, NULL));
        /* Labelled version 5, only the record minus the sound level is valid. */
        cut[4] = 5u;
        repair(cut, length);
        bool v5 = decode_checked(cut, length, &loaded);
        CHECK(v5 == (length == size - 1u));
        if (v5) /* Pre-v6 saves load the middle level; the battery setting is kept. */
            CHECK(loaded.game.sound_level == JELLI_SOUND_LEVEL_DEFAULT &&
                  !loaded.game.battery_display);
        /* Labelled version 3, only the record minus both settings bytes is valid. */
        cut[4] = 3u;
        repair(cut, length);
        bool v3 = decode_checked(cut, length, &loaded);
        CHECK(v3 == (length == size - 2u));
        if (v3) /* Pre-v4 saves show the indicator. */
            CHECK(loaded.game.battery_display &&
                  loaded.game.sound_level == JELLI_SOUND_LEVEL_DEFAULT);
    }
    for (size_t extra = 1u; extra <= 16u && size + extra <= JELLI_SAVE_CAPACITY; ++extra) {
        memcpy(cut, bytes, size - TRAILER);
        memset(cut + size - TRAILER, 0, extra);
        memcpy(cut + size - TRAILER + extra, bytes + size - TRAILER, TRAILER);
        repair(cut, size + extra);
        CHECK(!decode_checked(cut, size + extra, NULL)); /* Trailing bytes are not ignored. */
    }
    CHECK(!decode_checked(bytes, JELLI_SAVE_CAPACITY + 1u, NULL));
}

static void every_setting_and_version_byte(void)
{
    JelliSave save = fixture(true, JELLI_SOUND_LEVEL_DEFAULT), loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY], mutated[JELLI_SAVE_CAPACITY];
    size_t size = encode(&save, bytes);
    for (unsigned value = 0u; value < 256u; ++value) {
        memcpy(mutated, bytes, size);
        mutated[size - SOUND_FROM_END] = (uint8_t)value;
        repair(mutated, size);
        bool level = decode_checked(mutated, size, &loaded);
        CHECK(level == (value <= JELLI_SOUND_LEVEL_MAX)); /* 11..255 are corrupt. */
        if (level)
            CHECK(loaded.game.sound_level == value && canonical(mutated, size));
        memcpy(mutated, bytes, size);
        mutated[size - SETTING_FROM_END] = (uint8_t)value;
        repair(mutated, size);
        bool accepted = decode_checked(mutated, size, &loaded);
        CHECK(accepted == (value <= 1u)); /* Strict boolean: 2..255 are corrupt. */
        if (accepted)
            CHECK(loaded.game.battery_display == (value == 1u) && canonical(mutated, size));
        memcpy(mutated, bytes, size);
        mutated[4] =
            (uint8_t)value; /* Only version 6 carries the sound level: 4, 5, 7, ... fail. */
        repair(mutated, size);
        CHECK(decode_checked(mutated, size, &loaded) == (value == JELLI_SAVE_VERSION));
        memcpy(mutated, bytes, size);
        mutated[5] = (uint8_t)value; /* High byte of the little-endian version. */
        repair(mutated, size);
        CHECK(decode_checked(mutated, size, NULL) == (value == 0u));
    }
}

static void every_single_bit_flip(void)
{
    JelliSave save = fixture(true, JELLI_SOUND_LEVEL_DEFAULT);
    uint8_t bytes[JELLI_SAVE_CAPACITY], mutated[JELLI_SAVE_CAPACITY];
    size_t size = encode(&save, bytes);
    unsigned accepted = 0u;
    for (size_t index = 0; index < size; ++index) {
        for (unsigned bit = 0; bit < 8u; ++bit) {
            memcpy(mutated, bytes, size);
            mutated[index] ^= (uint8_t)(1u << bit);
            CHECK(!decode_checked(mutated, size, NULL)); /* CRC-32 catches every 1-bit error. */
            repair(mutated, size);
            if (canonical(mutated, size))
                ++accepted;
            else
                CHECK(!decode_checked(mutated, size, NULL));
        }
    }
    /* Flipping a battery bit 0 is a legal setting change, so some flips must load. */
    CHECK(accepted > 0u && accepted < size * 8u);
}

/* Each stored level 0..10 with each single bit of the sound byte flipped and the CRC
 * repaired: the result loads exactly when the new level is still 0..10, and then
 * round-trips canonically; otherwise the output is untouched (decode_checked). */
static void sound_byte_bit_flips(void)
{
    uint8_t bytes[JELLI_SAVE_CAPACITY], mutated[JELLI_SAVE_CAPACITY];
    unsigned accepted = 0u;
    for (unsigned level = 0u; level <= JELLI_SOUND_LEVEL_MAX; ++level) {
        JelliSave save = fixture((level & 1u) != 0u, (uint8_t)level), loaded;
        size_t size = encode(&save, bytes);
        CHECK(bytes[size - SOUND_FROM_END] == level);
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            unsigned flipped = level ^ (1u << bit);
            memcpy(mutated, bytes, size);
            mutated[size - SOUND_FROM_END] = (uint8_t)flipped;
            CHECK(!decode_checked(mutated, size, NULL)); /* The CRC still guards it. */
            repair(mutated, size);
            bool ok = decode_checked(mutated, size, &loaded);
            CHECK(ok == (flipped <= JELLI_SOUND_LEVEL_MAX));
            if (ok)
                CHECK(loaded.game.sound_level == flipped && canonical(mutated, size) &&
                      loaded.game.battery_display == ((level & 1u) != 0u));
            accepted += ok ? 1u : 0u;
        }
    }
    CHECK(accepted == 34u); /* 34 of the 88 single-bit flips of 0..10 stay within 0..10. */
}

static size_t mutate(uint8_t *bytes, size_t size, uint32_t *seed)
{
    static const uint8_t interesting[] = {0u, 1u, 2u, 0x7fu, 0x80u, 0xffu};
    unsigned rounds = 1u + next_random(seed) % 4u;
    for (unsigned r = 0; r < rounds; ++r) {
        uint32_t choice = next_random(seed) % 5u, at = next_random(seed);
        size_t index = at % size;
        size_t body = HEADER + at % (size - HEADER - TRAILER); /* Inside the game record. */
        if (choice == 0u) {
            bytes[index] = (uint8_t)next_random(seed);
        } else if (choice == 1u) {
            bytes[index] = interesting[next_random(seed) % sizeof(interesting)];
        } else if (choice == 2u) {
            bytes[index] ^= (uint8_t)(1u << (next_random(seed) % 8u));
        } else if (choice == 3u && size > HEADER + TRAILER + 1u) {
            memmove(bytes + body, bytes + body + 1u, size - body - 1u); /* Delete one byte. */
            --size;
        } else if (choice == 4u && size < JELLI_SAVE_CAPACITY) {
            memmove(bytes + body + 1u, bytes + body, size - body); /* Insert one byte. */
            bytes[body] = (uint8_t)next_random(seed);
            ++size;
        }
    }
    return size;
}

static void seeded_mutation_fuzz(void)
{
    const uint32_t seeds[] = {1u, 0x0badf00du, 0xc04d17edu};
    uint8_t mutated[JELLI_SAVE_CAPACITY + 1u];
    unsigned accepted = 0u, total = 0u;
    for (unsigned s = 0; s < sizeof(seeds) / sizeof(seeds[0]); ++s) {
        uint32_t state = seeds[s];
        for (unsigned i = 0; i < MUTATIONS; ++i) {
            JelliSave save = fixture((i & 1u) != 0u, (uint8_t)(i % (JELLI_SOUND_LEVEL_MAX + 1u)));
            size_t size = encode(&save, mutated);
            size = mutate(mutated, size, &state);
            if (next_random(&state) % 8u != 0u)
                repair(mutated, size);
            /* Never crashes (sanitizer run), rejects cleanly, or yields a canonical record. */
            bool first = decode_checked(mutated, size, NULL);
            CHECK_CASE(first == decode_checked(mutated, size, NULL), seeds[s], i);
            if (first)
                CHECK_CASE(canonical(mutated, size), seeds[s], i);
            accepted += first ? 1u : 0u;
            ++total;
        }
    }
    /* Repaired framing must let some mutants through to the field checks. */
    CHECK(accepted > 0u && accepted < total);
    printf("mutation fuzz: %u of %u mutants decoded canonically\n", accepted, total);
}

static void random_records(void)
{
    uint32_t state = UINT32_C(0x00c0ffee);
    static uint8_t bytes[JELLI_SAVE_CAPACITY + 1u];
    for (unsigned i = 0; i < 2000u; ++i) {
        size_t size = next_random(&state) % (JELLI_SAVE_CAPACITY + 2u);
        for (size_t b = 0; b < size; ++b)
            bytes[b] = (uint8_t)next_random(&state);
        if (size >= HEADER + TRAILER && (i & 1u)) {
            /* Valid framing around random content reaches the field decoder. */
            memcpy(bytes, "JLSV", 4u);
            bytes[4] = (uint8_t)(1u + next_random(&state) % JELLI_SAVE_VERSION);
            bytes[5] = 0u;
            bytes[6] = HEADER;
            bytes[7] = 0u;
            memcpy(bytes + size - 4u, "\xed\x17\x4d\xc0", 4u);
            repair(bytes, size);
        }
        CHECK_CASE(!decode_checked(bytes, size, NULL), UINT32_C(0x00c0ffee), i);
    }
}

static void invalid_arguments(void)
{
    JelliSave save = fixture(true, JELLI_SOUND_LEVEL_MAX), out = fixture(false, 0u), scratch;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = encode(&save, bytes);
    CHECK(!jelli_save_decode(NULL, bytes, size));
    CHECK(!jelli_save_decode(&out, NULL, size));
    CHECK(!jelli_save_decode_workspace(&out, bytes, size, NULL));
    CHECK(!jelli_save_decode_workspace(&out, bytes, size, &out));
    CHECK(!out.game.battery_display && !out.game.sound_level); /* Unchanged by rejections. */
    CHECK(jelli_save_decode_workspace(&out, bytes, size, &scratch) && out.game.battery_display);
    CHECK(out.game.sound_level == JELLI_SOUND_LEVEL_MAX);
    CHECK(jelli_save_encode(&save, bytes, size - 1u) == 0u); /* One byte short of v6. */
    CHECK(jelli_save_encode(&save, bytes, size) == size);
}

int main(void)
{
    round_trip_property();
    every_truncation_and_extension();
    every_setting_and_version_byte();
    every_single_bit_flip();
    sound_byte_bit_flips();
    seeded_mutation_fuzz();
    random_records();
    invalid_arguments();
    puts("PASS: v6 save codec truncation, bit-flip, byte-sweep, and seeded mutation properties");
    return 0;
}

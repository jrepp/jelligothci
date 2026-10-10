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

static uint32_t test_checksum(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? UINT32_C(0xedb88320) : 0u);
    }
    return ~crc;
}

static void repair_checksum(uint8_t *bytes, size_t size)
{
    uint32_t crc = test_checksum(bytes, size - 8u);
    for (unsigned i = 0; i < 4u; ++i)
        bytes[size - 8u + i] = (uint8_t)(crc >> (8u * i));
}

static size_t encode(const JelliSave *save, uint8_t *bytes)
{
    size_t size = jelli_save_encode(save, bytes, JELLI_SAVE_CAPACITY);
    CHECK(size != 0u);
    return size;
}

static bool unchanged(const JelliSave *left, const JelliSave *right)
{
    uint8_t a[JELLI_SAVE_CAPACITY];
    uint8_t b[JELLI_SAVE_CAPACITY];
    size_t size_a = jelli_save_encode(left, a, sizeof(a));
    size_t size_b = jelli_save_encode(right, b, sizeof(b));
    return size_a != 0u && size_a == size_b && memcmp(a, b, size_a) == 0;
}

static JelliSave fixture(void)
{
    JelliSave save = {.sequence = UINT64_C(0x123456789abcdef0),
                      .anchor_ms = UINT64_C(0x1020304050607080),
                      .anchor_valid = true};
    jelli_game_init(&save.game);
    save.game.ticks = 12000u;
    save.game.discarded_ms = 1800u;
    save.game.backlog_ms = 50u;
    save.game.revision = 11u;
    save.game.timezone_minutes = -330;
    save.game.clock_adjust = 25;
    save.game.prizes.owned = 1u;
    save.game.prizes.discovered = 3u;
    save.game.prizes.origin_pet[0] = 1u;
    save.game.prizes.origin_pet[1] = 2u;
    save.game.prizes.offered = 3u;
    save.game.prizes.offered_pet = 2u;
    save.game.pets[0].prize_progress.counts[0] = 3u;
    save.game.pets[1].prize_progress.counts[1] = 2u;
    save.game.pets[1].prize_progress.breakfast_day_known = true;
    save.game.pets[1].prize_progress.last_breakfast_day = 12u;
    save.game.pets[0].hunger_due = 60000u;
    save.game.pets[0].hunger_low = true;
    for (size_t i = 0; i < JELLI_NEED_COUNT; ++i)
        save.game.pets[0].need_remainders[i] = (uint16_t)(10u + i);
    save.game.pets[0].reward_claimed = true;
    save.game.pets[1].reward_pending = true;
    save.game.sleep_log =
        (JelliSleepLog){.total_seconds = 3600u, .head = 1u, .count = 1u, .pet_id = 1u};
    save.game.sleep_log.sessions[0] = (JelliSleepSession){.bed_unix_seconds = 1700000000u,
                                                          .wake_unix_seconds = 1700003600u,
                                                          .duration_seconds = 3600u,
                                                          .flags = 7u};
    return save;
}

static void round_trip_is_canonical(void)
{
    JelliSave original = fixture();
    JelliSave decoded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    uint8_t roundtrip[JELLI_SAVE_CAPACITY];
    size_t size = encode(&original, bytes);
    CHECK(jelli_save_decode(&decoded, bytes, size));
    size_t encoded_again = encode(&decoded, roundtrip);
    CHECK(encoded_again == size && memcmp(bytes, roundtrip, size) == 0);
    CHECK(decoded.sequence == original.sequence && decoded.anchor_valid);
    CHECK(decoded.game.backlog_ms == 50u && decoded.game.pets[0].hunger_low);
    CHECK(decoded.game.pets[0].need_remainders[0] == 10u);
    CHECK(decoded.game.pets[0].reward_claimed && decoded.game.pets[1].reward_pending);
    CHECK(decoded.game.sleep_log.total_seconds == 3600u);
    CHECK(decoded.game.sleep_log.sessions[0].wake_unix_seconds == 1700003600u);
    CHECK(decoded.game.timezone_minutes == -330 && decoded.game.clock_adjust == 25);
    CHECK(decoded.game.prizes.owned == 1u && decoded.game.prizes.discovered == 3u);
    CHECK(decoded.game.prizes.origin_pet[1] == 2u && decoded.game.prizes.offered == 3u);
    CHECK(decoded.game.pets[1].prize_progress.last_breakfast_day == 12u);
    CHECK(jelli_sleep_log_begin(&original.game.sleep_log, 1u, original.game.pets[0].ticks, true,
                                1700086400u));
    original.game.wall_seconds = 1700086400u;
    original.game.wall_known = true;
    size = encode(&original, bytes);
    CHECK(jelli_save_decode(&decoded, bytes, size));
    CHECK(decoded.game.sleep_log.active && decoded.game.sleep_log.count == 2u);
    CHECK(decoded.game.sleep_log.sessions[1].bed_unix_seconds == 1700086400u);
    CHECK(!decoded.game.wall_known && decoded.game.wall_seconds == 0u);
}

static void failed_decodes_preserve_output(void)
{
    JelliSave original = fixture();
    JelliSave output = fixture();
    output.sequence = 7u;
    output.anchor_valid = false;
    JelliSave saved_output = output;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    uint8_t broken[JELLI_SAVE_CAPACITY];
    size_t size = encode(&original, bytes);
    for (size_t cut = 0; cut < size; ++cut) {
        CHECK(!jelli_save_decode(&output, bytes, cut));
        CHECK(unchanged(&output, &saved_output));
    }
    memcpy(broken, bytes, size);
    broken[size / 2u] ^= 0x80u;
    CHECK(!jelli_save_decode(&output, broken, size));
    CHECK(unchanged(&output, &saved_output));
    memcpy(broken, bytes, size);
    broken[4] = JELLI_SAVE_VERSION + 1u;
    CHECK(!jelli_save_decode(&output, broken, size));
    CHECK(unchanged(&output, &saved_output));
    memcpy(broken, bytes, size);
    broken[29] = 2u;
    repair_checksum(broken, size);
    CHECK(!jelli_save_decode(&output, broken, size));
    CHECK(unchanged(&output, &saved_output));
    memcpy(broken, bytes, size);
    broken[size] = 0u;
    CHECK(!jelli_save_decode(&output, broken, size + 1u));
    CHECK(unchanged(&output, &saved_output));
}

static void invalid_fields_are_rejected(void)
{
    JelliSave original = fixture();
    JelliSave output = fixture();
    JelliSave saved_output = output;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = encode(&original, bytes);
    bytes[68] = UINT8_MAX;
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&output, bytes, size));
    CHECK(unchanged(&output, &saved_output));
    size = encode(&original, bytes);
    bytes[175] = UINT8_MAX;
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&output, bytes, size));
    CHECK(unchanged(&output, &saved_output));
    size = encode(&original, bytes);
    bytes[64] = 0u;
    bytes[65] = 0u;
    bytes[176] = (uint8_t)JELLI_EATING;
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&output, bytes, size));
    CHECK(unchanged(&output, &saved_output));
    size = encode(&original, bytes);
    bytes[157] = 0x58u;
    bytes[158] = 0x02u;
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&output, bytes, size));
    CHECK(unchanged(&output, &saved_output));
    size = encode(&original, bytes);
    bytes[56] = 100u;
    bytes[57] = 0u;
    bytes[58] = 0u;
    bytes[59] = 0u;
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&output, bytes, size));
    CHECK(unchanged(&output, &saved_output));
    original.game.backlog_ms = 100u;
    CHECK(jelli_save_encode(&original, bytes, sizeof(bytes)) == 0u);
    original.game.backlog_ms = 50u;
    original.game.resume_remaining_ms = 1u;
    CHECK(jelli_save_encode(&original, bytes, sizeof(bytes)) == 0u);
    original.game.resuming = true;
    CHECK(jelli_save_encode(&original, bytes, sizeof(bytes)) == 0u);
}

static void advance_ms(JelliGame *game, uint64_t milliseconds)
{
    while (milliseconds > 0u) {
        uint64_t step = milliseconds > 800u ? 800u : milliseconds;
        jelli_game_advance(game, step);
        milliseconds -= step;
    }
}

static void check_same_save(const JelliSave *left, const JelliSave *right)
{
    uint8_t left_bytes[JELLI_SAVE_CAPACITY];
    uint8_t right_bytes[JELLI_SAVE_CAPACITY];
    size_t left_size = encode(left, left_bytes);
    size_t right_size = encode(right, right_bytes);
    CHECK(left_size == right_size && memcmp(left_bytes, right_bytes, left_size) == 0);
}

static void interaction_claim_and_sleep_round_trip(void)
{
    JelliSave saved = fixture();
    JelliSave loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    CHECK(jelli_game_command(&saved.game, (JelliCommand){.kind = JELLI_CMD_PLAY, .actor_id = 1u}) ==
          JELLI_OK);
    size_t size = encode(&saved, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].activity == JELLI_PLAYING);
    CHECK(loaded.game.pets[0].interaction_due == saved.game.pets[0].interaction_due);
    advance_ms(&saved.game, 7950u);
    advance_ms(&loaded.game, 7950u);
    CHECK(saved.game.pets[0].activity == JELLI_IDLE && loaded.game.pets[0].activity == JELLI_IDLE);
    check_same_save(&saved, &loaded);

    saved.game.active = 1u;
    CHECK(jelli_game_command(&saved.game,
                             (JelliCommand){.kind = JELLI_CMD_CLAIM, .actor_id = 2u}) == JELLI_OK);
    size = encode(&saved, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    uint16_t food_after_claim = loaded.game.food;
    CHECK(loaded.game.pets[1].reward_claimed && !loaded.game.pets[1].reward_pending);
    CHECK(jelli_game_command(&loaded.game, (JelliCommand){.kind = JELLI_CMD_CLAIM,
                                                          .actor_id = 2u}) == JELLI_NOT_READY);
    CHECK(loaded.game.food == food_after_claim);

    saved = fixture();
    CHECK(jelli_game_command(&saved.game, (JelliCommand){.kind = JELLI_CMD_REST, .actor_id = 1u}) ==
          JELLI_OK);
    size = encode(&saved, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].asleep && !loaded.game.pets[0].scheduled_sleep);
    CHECK(loaded.game.pets[0].nap_due == saved.game.pets[0].nap_due);
    advance_ms(&saved.game, 200u);
    advance_ms(&loaded.game, 200u);
    CHECK(saved.game.pets[0].asleep && loaded.game.pets[0].asleep);
    CHECK(jelli_game_command(&saved.game, (JelliCommand){.kind = JELLI_CMD_WAKE, .actor_id = 1u}) ==
          JELLI_OK);
    CHECK(jelli_game_command(&loaded.game,
                             (JelliCommand){.kind = JELLI_CMD_WAKE, .actor_id = 1u}) == JELLI_OK);
    check_same_save(&saved, &loaded);
}

static void need_rate_phase_survives_save(void)
{
    JelliSave saved = fixture();
    JelliSave loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    for (size_t i = 0; i < JELLI_NEED_COUNT; ++i)
        saved.game.pets[0].need_remainders[i] = 0u;
    advance_ms(&saved.game, 400u);
    size_t size = encode(&saved, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].need_remainders[JELLI_SATIETY] == 4u);
    CHECK(loaded.game.pets[0].need_remainders[JELLI_ENERGY] == 16u);
    CHECK(loaded.game.pets[0].need_remainders[JELLI_HYGIENE] == 1u);
    advance_ms(&saved.game, 59600u);
    advance_ms(&loaded.game, 59600u);
    CHECK(saved.game.pets[0].needs[JELLI_SATIETY] == 499u);
    CHECK(saved.game.pets[0].needs[JELLI_ENERGY] == 696u);
    CHECK(saved.game.pets[0].needs[JELLI_HYGIENE] == 700u);
    check_same_save(&saved, &loaded);
}

static void health_history_and_v1_migration(void)
{
    JelliSave save = fixture(), loaded;
    JelliPet *pet = &save.game.pets[0];
    pet->shot_until = pet->ticks + 36000u;
    pet->medicine_until = pet->ticks + 18000u;
    pet->shot_goal = 3u;
    pet->shot_hits = 1u;
    uint8_t bytes[JELLI_SAVE_CAPACITY], legacy[JELLI_SAVE_CAPACITY];
    size_t size = encode(&save, bytes);
    CHECK(bytes[4] == JELLI_SAVE_VERSION && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].shot_until == pet->shot_until);
    CHECK(loaded.game.pets[0].medicine_until == pet->medicine_until);
    CHECK(loaded.game.pets[0].shot_goal == 3u && loaded.game.pets[0].shot_hits == 1u);
    size_t pet_size = 130u + JELLI_HABIT_BIN_COUNT * 6u + 37u + JELLI_PRIZE_COUNT * 2u + 9u;
    for (unsigned version = 1u; version <= 2u; ++version) {
        /* Both legacy versions share the header/game prefix. */
        size_t legacy_pet_size = version == 1u ? 112u : 130u;
        memcpy(legacy, bytes, 71u);
        size_t old_size = 71u;
        for (unsigned i = 0; i < save.game.count; ++i) {
            memcpy(legacy + old_size, bytes + 71u + (size_t)i * pet_size, legacy_pet_size);
            old_size += legacy_pet_size;
        }
        memcpy(legacy + old_size, bytes + size - 8u, 8u);
        old_size += 8u;
        legacy[4] = (uint8_t)version;
        for (unsigned i = 0; i < 4u; ++i)
            legacy[8u + i] = (uint8_t)(old_size >> (i * 8u));
        repair_checksum(legacy, old_size);
        CHECK(jelli_save_decode(&loaded, legacy, old_size));
        CHECK(loaded.game.pets[0].shot_until == (version == 1u ? 0u : pet->shot_until));
        CHECK(loaded.game.pets[0].medicine_until == (version == 1u ? 0u : pet->medicine_until));
        CHECK(loaded.game.pets[0].shot_goal == (version == 1u ? 0u : 3u));
        CHECK(loaded.game.pets[0].shot_hits == (version == 1u ? 0u : 1u));
        CHECK(loaded.game.sleep_log.count == 0u && !loaded.game.sleep_log.active);
        CHECK(loaded.game.timezone_minutes == 0 && loaded.game.clock_adjust == 0);
        CHECK(loaded.game.prizes.owned == 0u && loaded.game.prizes.discovered == 0u);
        CHECK(loaded.game.pets[0].prize_progress.counts[0] == 0u);
        CHECK(loaded.game.pets[0].habits.observed_ticks == 0u);
        CHECK(loaded.game.pets[0].habits.lifetime_sleep_ticks == 0u);
        CHECK(loaded.game.pets[0].habits.lifetime_play_ticks == 0u);
        CHECK(loaded.game.pets[0].habits.lifetime_meals == 0u);
        CHECK(loaded.game.pets[0].bond == pet->bond && loaded.sequence == save.sequence);
    }
}

static void rolling_history_round_trip(void)
{
    JelliSave save = fixture(), loaded;
    for (unsigned p = 0u; p < save.game.count; ++p) {
        JelliPet *pet = &save.game.pets[p];
        for (unsigned hour = 0u; hour < 28u; ++hour) {
            uint64_t start = (uint64_t)hour * JELLI_HABIT_HOUR_TICKS;
            jelli_habits_advance(&pet->habits, start, JELLI_HABIT_HOUR_TICKS, hour % 3u == p,
                                 hour % 3u == 2u);
            jelli_habits_record_meal(&pet->habits, start + JELLI_HABIT_HOUR_TICKS);
        }
        pet->ticks = pet->habits.cursor_ticks;
    }
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = encode(&save, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    check_same_save(&save, &loaded);
    for (unsigned p = 0u; p < save.game.count; ++p) {
        CHECK(loaded.game.pets[p].habits.lifetime_meals == 28u);
        CHECK(loaded.game.pets[p].habits.head == 3u);
        JelliHabitTotals expected = jelli_habits_totals(&save.game.pets[p].habits);
        JelliHabitTotals actual = jelli_habits_totals(&loaded.game.pets[p].habits);
        CHECK(actual.sleep_ticks == expected.sleep_ticks &&
              actual.play_ticks == expected.play_ticks);
        CHECK(actual.meals_q16 == expected.meals_q16 &&
              actual.coverage_ticks == expected.coverage_ticks);
    }
    JelliSave before = loaded;
    /* First pet's rolling-bin sleep duration cannot exceed one hour. */
    bytes[201] = UINT8_MAX;
    bytes[202] = UINT8_MAX;
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size));
    CHECK(unchanged(&loaded, &before));
    size = encode(&save, bytes);
    bytes[387] = JELLI_HABIT_BIN_COUNT; /* Ring head follows 150-byte bins and 36-byte totals. */
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size));
    CHECK(unchanged(&loaded, &before));
    size = encode(&save, bytes);
    bytes[size - 69u] = 2u; /* Owned bit nine is outside the nine prize slots. */
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size));
    CHECK(unchanged(&loaded, &before));
    size = encode(&save, bytes);
    bytes[size - 30u] = JELLI_PRIZE_COUNT + 1u;
    repair_checksum(bytes, size);
    CHECK(!jelli_save_decode(&loaded, bytes, size));
    CHECK(unchanged(&loaded, &before));
    save.game.count = JELLI_PET_CAPACITY;
    for (unsigned p = 2u; p < save.game.count; ++p) {
        save.game.pets[p] = save.game.pets[0];
        save.game.pets[p].id = p + 1u;
        save.game.pets[p].collection_entry = (uint8_t)(p + 1u);
    }
    size = encode(&save, bytes);
    CHECK(size <= JELLI_SAVE_CAPACITY && jelli_save_decode(&loaded, bytes, size));
    check_same_save(&save, &loaded);
}

int main(void)
{
    rolling_history_round_trip();
    health_history_and_v1_migration();
    round_trip_is_canonical();
    failed_decodes_preserve_output();
    invalid_fields_are_rejected();
    interaction_claim_and_sleep_round_trip();
    need_rate_phase_survives_save();
    puts("PASS: explicit save codec, canonical round trip, malformed input rejection");
    return 0;
}

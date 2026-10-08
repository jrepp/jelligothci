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
    save.game.pets[0].hunger_due = 60000u;
    save.game.pets[0].hunger_low = true;
    for (size_t i = 0; i < JELLI_NEED_COUNT; ++i)
        save.game.pets[0].need_remainders[i] = (uint16_t)(10u + i);
    save.game.pets[0].reward_claimed = true;
    save.game.pets[1].reward_pending = true;
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
    broken[4] = 3u;
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
    saved.game.pets[0].nap_due = saved.game.pets[0].ticks + 2u;
    size = encode(&saved, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].asleep && !loaded.game.pets[0].scheduled_sleep);
    CHECK(loaded.game.pets[0].nap_due == saved.game.pets[0].nap_due);
    advance_ms(&saved.game, 200u);
    advance_ms(&loaded.game, 200u);
    CHECK(!saved.game.pets[0].asleep && !loaded.game.pets[0].asleep);
    check_same_save(&saved, &loaded);
}

static void need_rate_phase_survives_save(void)
{
    JelliSave saved = fixture();
    JelliSave loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    for (size_t i = 0; i < JELLI_NEED_COUNT; ++i)
        saved.game.pets[0].need_remainders[i] = 0u;
    advance_ms(&saved.game, 100u);
    size_t size = encode(&saved, bytes);
    CHECK(jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].need_remainders[JELLI_SATIETY] == 8u);
    CHECK(loaded.game.pets[0].need_remainders[JELLI_ENERGY] == 4u);
    CHECK(loaded.game.pets[0].need_remainders[JELLI_HYGIENE] == 3u);
    advance_ms(&saved.game, 59900u);
    advance_ms(&loaded.game, 59900u);
    CHECK(saved.game.pets[0].needs[JELLI_SATIETY] == 492u);
    CHECK(saved.game.pets[0].needs[JELLI_ENERGY] == 696u);
    CHECK(saved.game.pets[0].needs[JELLI_HYGIENE] == 697u);
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
    CHECK(bytes[4] == 2u && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[0].shot_until == pet->shot_until);
    CHECK(loaded.game.pets[0].medicine_until == pet->medicine_until);
    CHECK(loaded.game.pets[0].shot_goal == 3u && loaded.game.pets[0].shot_hits == 1u);
    /* V1 has the same header/game prefix and 112-byte pet records, without history. */
    memcpy(legacy, bytes, 71u);
    size_t old_size = 71u;
    for (unsigned i = 0; i < save.game.count; ++i) {
        memcpy(legacy + old_size, bytes + 71u + (size_t)i * 130u, 112u);
        old_size += 112u;
    }
    memcpy(legacy + old_size, bytes + size - 8u, 8u);
    old_size += 8u;
    legacy[4] = 1u;
    for (unsigned i = 0; i < 4u; ++i)
        legacy[8u + i] = (uint8_t)(old_size >> (i * 8u));
    repair_checksum(legacy, old_size);
    CHECK(jelli_save_decode(&loaded, legacy, old_size));
    CHECK(loaded.game.pets[0].shot_until == 0u && loaded.game.pets[0].medicine_until == 0u);
    CHECK(loaded.game.pets[0].shot_goal == 0u && loaded.game.pets[0].shot_hits == 0u);
    CHECK(loaded.game.pets[0].bond == pet->bond && loaded.sequence == save.sequence);
}

int main(void)
{
    health_history_and_v1_migration();
    round_trip_is_canonical();
    failed_decodes_preserve_output();
    invalid_fields_are_rejected();
    interaction_claim_and_sleep_round_trip();
    need_rate_phase_survives_save();
    puts("PASS: explicit save codec, canonical round trip, malformed input rejection");
    return 0;
}

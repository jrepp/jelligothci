#include "jelli/save.h"

#include <string.h>

#define SAVE_VERSION 2u
#define SAVE_CONTENT_VERSION 1u
#define SAVE_HEADER_SIZE 32u
#define SAVE_TRAILER_SIZE 8u

typedef struct {
    uint8_t *bytes;
    size_t size;
    size_t offset;
    bool failed;
} Writer;

typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
    bool failed;
    uint8_t version;
} Reader;

static void put_u8(Writer *writer, uint8_t value)
{
    if (writer->offset >= writer->size) {
        writer->failed = true;
        return;
    }
    writer->bytes[writer->offset++] = value;
}

static void put_u16(Writer *writer, uint16_t value)
{
    put_u8(writer, (uint8_t)(value & 0xffu));
    put_u8(writer, (uint8_t)(value >> 8u));
}

static void put_u32(Writer *writer, uint32_t value)
{
    put_u16(writer, (uint16_t)(value & 0xffffu));
    put_u16(writer, (uint16_t)(value >> 16u));
}

static void put_u64(Writer *writer, uint64_t value)
{
    put_u32(writer, (uint32_t)(value & UINT64_C(0xffffffff)));
    put_u32(writer, (uint32_t)(value >> 32u));
}

static uint8_t get_u8(Reader *reader)
{
    if (reader->offset >= reader->size) {
        reader->failed = true;
        return 0u;
    }
    return reader->bytes[reader->offset++];
}

static uint16_t get_u16(Reader *reader)
{
    uint16_t low = get_u8(reader);
    uint16_t high = get_u8(reader);
    return (uint16_t)(low | (uint16_t)(high << 8u));
}

static uint32_t get_u32(Reader *reader)
{
    uint32_t low = get_u16(reader);
    uint32_t high = get_u16(reader);
    return low | (high << 16u);
}

static uint64_t get_u64(Reader *reader)
{
    uint64_t low = get_u32(reader);
    uint64_t high = get_u32(reader);
    return low | (high << 32u);
}

static void write_pet(Writer *writer, const JelliPet *pet)
{
    put_u32(writer, pet->id);
    put_u64(writer, pet->ticks);
    put_u64(writer, pet->stage_ticks);
    put_u64(writer, pet->interaction_due);
    put_u64(writer, pet->nap_due);
    put_u64(writer, pet->awake_until);
    put_u64(writer, pet->wake_override_until);
    put_u64(writer, pet->hunger_due);
    put_u32(writer, pet->phase_offset);
    put_u32(writer, pet->bedtime);
    put_u32(writer, pet->sleep_duration);
    put_u32(writer, pet->random_state);
    for (size_t i = 0; i < JELLI_NEED_COUNT; ++i)
        put_u16(writer, pet->needs[i]);
    for (size_t i = 0; i < JELLI_NEED_COUNT; ++i)
        put_u16(writer, pet->need_remainders[i]);
    put_u16(writer, pet->bond);
    put_u16(writer, pet->feeds);
    put_u16(writer, pet->neglect);
    put_u8(writer, pet->form);
    put_u8(writer, pet->location);
    put_u8(writer, (uint8_t)pet->health);
    put_u8(writer, (uint8_t)pet->activity);
    put_u8(writer, pet->asleep ? 1u : 0u);
    put_u8(writer, pet->scheduled_sleep ? 1u : 0u);
    put_u8(writer, pet->hunger_low ? 1u : 0u);
    put_u8(writer, pet->hunger_counted ? 1u : 0u);
    put_u8(writer, pet->reward_pending ? 1u : 0u);
    put_u8(writer, pet->reward_claimed ? 1u : 0u);
    put_u64(writer, pet->shot_until);
    put_u64(writer, pet->medicine_until);
    put_u8(writer, pet->shot_goal);
    put_u8(writer, pet->shot_hits);
}

static bool read_bool(Reader *reader, bool *value)
{
    uint8_t raw = get_u8(reader);
    if (raw > 1u) {
        reader->failed = true;
        return false;
    }
    *value = raw != 0u;
    return !reader->failed;
}

static void read_pet(Reader *reader, JelliPet *pet)
{
    pet->id = get_u32(reader);
    pet->ticks = get_u64(reader);
    pet->stage_ticks = get_u64(reader);
    pet->interaction_due = get_u64(reader);
    pet->nap_due = get_u64(reader);
    pet->awake_until = get_u64(reader);
    pet->wake_override_until = get_u64(reader);
    pet->hunger_due = get_u64(reader);
    pet->phase_offset = get_u32(reader);
    pet->bedtime = get_u32(reader);
    pet->sleep_duration = get_u32(reader);
    pet->random_state = get_u32(reader);
    for (size_t i = 0; i < JELLI_NEED_COUNT; ++i)
        pet->needs[i] = get_u16(reader);
    for (size_t i = 0; i < JELLI_NEED_COUNT; ++i)
        pet->need_remainders[i] = get_u16(reader);
    pet->bond = get_u16(reader);
    pet->feeds = get_u16(reader);
    pet->neglect = get_u16(reader);
    pet->form = get_u8(reader);
    pet->location = get_u8(reader);
    pet->health = (JelliHealth)get_u8(reader);
    pet->activity = (JelliActivity)get_u8(reader);
    (void)read_bool(reader, &pet->asleep);
    (void)read_bool(reader, &pet->scheduled_sleep);
    (void)read_bool(reader, &pet->hunger_low);
    (void)read_bool(reader, &pet->hunger_counted);
    (void)read_bool(reader, &pet->reward_pending);
    (void)read_bool(reader, &pet->reward_claimed);
    if (reader->version >= 2u) {
        pet->shot_until = get_u64(reader);
        pet->medicine_until = get_u64(reader);
        pet->shot_goal = get_u8(reader);
        pet->shot_hits = get_u8(reader);
    }
}

static void write_game(Writer *writer, const JelliGame *game)
{
    put_u64(writer, game->ticks);
    put_u64(writer, game->discarded_ms);
    put_u64(writer, game->resume_remaining_ms);
    put_u32(writer, game->backlog_ms);
    put_u32(writer, game->revision);
    put_u16(writer, game->food);
    put_u16(writer, game->gifts);
    put_u8(writer, game->count);
    put_u8(writer, game->active);
    put_u8(writer, game->resuming ? 1u : 0u);
    for (size_t i = 0; i < game->count; ++i)
        write_pet(writer, &game->pets[i]);
}

static bool read_game(Reader *reader, JelliGame *game)
{
    game->ticks = get_u64(reader);
    game->discarded_ms = get_u64(reader);
    game->resume_remaining_ms = get_u64(reader);
    game->backlog_ms = get_u32(reader);
    game->revision = get_u32(reader);
    game->food = get_u16(reader);
    game->gifts = get_u16(reader);
    game->count = get_u8(reader);
    game->active = get_u8(reader);
    (void)read_bool(reader, &game->resuming);
    if (game->count > JELLI_PET_CAPACITY) {
        reader->failed = true;
        return false;
    }
    for (size_t i = 0; i < game->count; ++i)
        read_pet(reader, &game->pets[i]);
    return !reader->failed;
}

static uint32_t checksum(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? UINT32_C(0xedb88320) : 0u);
    }
    return ~crc;
}

size_t jelli_save_encode(const JelliSave *save, uint8_t *bytes, size_t capacity)
{
    if (save == NULL || bytes == NULL || capacity < SAVE_HEADER_SIZE + SAVE_TRAILER_SIZE ||
        save->game.resuming || save->game.resume_remaining_ms != 0u ||
        save->game.backlog_ms >= 100u || !jelli_game_valid(&save->game))
        return 0u;
    Writer writer = {.bytes = bytes, .size = capacity};
    put_u8(&writer, (uint8_t)'J');
    put_u8(&writer, (uint8_t)'L');
    put_u8(&writer, (uint8_t)'S');
    put_u8(&writer, (uint8_t)'V');
    put_u16(&writer, SAVE_VERSION);
    put_u16(&writer, SAVE_HEADER_SIZE);
    put_u32(&writer, 0u);
    put_u64(&writer, save->sequence);
    put_u64(&writer, save->anchor_ms);
    put_u8(&writer, save->anchor_valid ? 1u : 0u);
    put_u8(&writer, SAVE_CONTENT_VERSION);
    put_u8(&writer, 0u);
    put_u8(&writer, 0u);
    write_game(&writer, &save->game);
    size_t total_size = writer.offset + SAVE_TRAILER_SIZE;
    if (writer.failed || total_size > JELLI_SAVE_CAPACITY || total_size > UINT32_MAX ||
        total_size > capacity)
        return 0u;
    uint32_t total32 = (uint32_t)total_size;
    for (size_t i = 0; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(total32 >> (8u * (unsigned)i));
    put_u32(&writer, checksum(bytes, writer.offset));
    put_u32(&writer, UINT32_C(0xc04d17ed));
    return writer.failed ? 0u : writer.offset;
}

static bool header_valid(const uint8_t *bytes, size_t size)
{
    return size >= SAVE_HEADER_SIZE + SAVE_TRAILER_SIZE && bytes[0] == (uint8_t)'J' &&
           bytes[1] == (uint8_t)'L' && bytes[2] == (uint8_t)'S' && bytes[3] == (uint8_t)'V' &&
           (bytes[4] == 1u || bytes[4] == SAVE_VERSION) && bytes[5] == 0u &&
           bytes[6] == SAVE_HEADER_SIZE && bytes[7] == 0u;
}

bool jelli_save_decode(JelliSave *save, const uint8_t *bytes, size_t size)
{
    if (save == NULL || bytes == NULL || size > JELLI_SAVE_CAPACITY || !header_valid(bytes, size))
        return false;
    Reader header = {.bytes = bytes, .size = size, .offset = 8u};
    uint32_t declared_size = get_u32(&header);
    if (declared_size != size)
        return false;
    Reader tail = {.bytes = bytes, .size = size, .offset = size - SAVE_TRAILER_SIZE};
    uint32_t stored_crc = get_u32(&tail);
    uint32_t marker = get_u32(&tail);
    if (marker != UINT32_C(0xc04d17ed) || stored_crc != checksum(bytes, size - SAVE_TRAILER_SIZE))
        return false;
    Reader reader = {.bytes = bytes, .size = size - SAVE_TRAILER_SIZE, .offset = SAVE_HEADER_SIZE};
    reader.version = bytes[4];
    JelliSave candidate;
    memset(&candidate, 0, sizeof(candidate));
    reader.offset = 12u;
    candidate.sequence = get_u64(&reader);
    candidate.anchor_ms = get_u64(&reader);
    (void)read_bool(&reader, &candidate.anchor_valid);
    if (get_u8(&reader) != SAVE_CONTENT_VERSION || get_u8(&reader) != 0u || get_u8(&reader) != 0u)
        reader.failed = true;
    (void)read_game(&reader, &candidate.game);
    if (reader.failed || reader.offset != reader.size || candidate.game.resuming ||
        candidate.game.resume_remaining_ms != 0u || candidate.game.backlog_ms >= 100u ||
        !jelli_game_valid(&candidate.game))
        return false;
    *save = candidate;
    return true;
}

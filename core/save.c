#include "jelli/save.h"
#include "save_codec.h"
#include "jelli/collection.h"

#include <string.h>

#define SAVE_VERSION 4u
#define SAVE_CONTENT_VERSION 1u
#define SAVE_HEADER_SIZE 32u
#define SAVE_TRAILER_SIZE 8u

static void write_habits(Writer *writer, const JelliHabits *habits)
{
    for (size_t i = 0; i < JELLI_HABIT_BIN_COUNT; ++i) {
        put_u16(writer, habits->bins[i].sleep_ticks);
        put_u16(writer, habits->bins[i].play_ticks);
        put_u16(writer, habits->bins[i].meals);
    }
    put_u64(writer, habits->lifetime_sleep_ticks);
    put_u64(writer, habits->lifetime_play_ticks);
    put_u64(writer, habits->observed_ticks);
    put_u64(writer, habits->cursor_ticks);
    put_u32(writer, habits->lifetime_meals);
    put_u8(writer, habits->head);
}

static void read_habits(Reader *reader, JelliHabits *habits)
{
    for (size_t i = 0; i < JELLI_HABIT_BIN_COUNT; ++i) {
        habits->bins[i].sleep_ticks = get_u16(reader);
        habits->bins[i].play_ticks = get_u16(reader);
        habits->bins[i].meals = get_u16(reader);
    }
    habits->lifetime_sleep_ticks = get_u64(reader);
    habits->lifetime_play_ticks = get_u64(reader);
    habits->observed_ticks = get_u64(reader);
    habits->cursor_ticks = get_u64(reader);
    habits->lifetime_meals = get_u32(reader);
    habits->head = get_u8(reader);
    if (!jelli_habits_valid(habits))
        reader->failed = true;
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
    write_habits(writer, &pet->habits);
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
        put_u16(writer, pet->prize_progress.counts[i]);
    put_u64(writer, pet->prize_progress.last_breakfast_day);
    put_u8(writer, pet->prize_progress.breakfast_day_known ? 1u : 0u);
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
    if (reader->version >= 3u) {
        read_habits(reader, &pet->habits);
        for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
            pet->prize_progress.counts[i] = get_u16(reader);
        pet->prize_progress.last_breakfast_day = get_u64(reader);
        (void)read_bool(reader, &pet->prize_progress.breakfast_day_known);
    }
}

static void write_sleep_log(Writer *writer, const JelliSleepLog *log)
{
    for (unsigned i = 0u; i < JELLI_SLEEP_SESSION_CAPACITY; ++i) {
        const JelliSleepSession *session = &log->sessions[i];
        put_u64(writer, session->bed_unix_seconds);
        put_u64(writer, session->wake_unix_seconds);
        put_u64(writer, session->start_tick);
        put_u32(writer, session->duration_seconds);
        put_u8(writer, session->flags);
        put_u16(writer, session->bed_energy);
        put_u16(writer, session->bed_sleep_score);
    }
    put_u64(writer, log->total_seconds);
    put_u8(writer, log->head);
    put_u8(writer, log->count);
    put_u8(writer, log->active ? 1u : 0u);
    put_u32(writer, log->pet_id);
}

static void read_sleep_log(Reader *reader, JelliSleepLog *log)
{
    for (unsigned i = 0u; i < JELLI_SLEEP_SESSION_CAPACITY; ++i) {
        JelliSleepSession *session = &log->sessions[i];
        session->bed_unix_seconds = get_u64(reader);
        session->wake_unix_seconds = get_u64(reader);
        session->start_tick = get_u64(reader);
        session->duration_seconds = get_u32(reader);
        session->flags = get_u8(reader);
        session->bed_energy = get_u16(reader);
        session->bed_sleep_score = get_u16(reader);
    }
    log->total_seconds = get_u64(reader);
    log->head = get_u8(reader);
    log->count = get_u8(reader);
    (void)read_bool(reader, &log->active);
    log->pet_id = get_u32(reader);
    if (!jelli_sleep_log_valid(log))
        reader->failed = true;
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
    write_sleep_log(writer, &game->sleep_log);
    put_u16(writer, (uint16_t)game->timezone_minutes);
    put_u16(writer, (uint16_t)game->clock_adjust);
    put_u16(writer, game->prizes.owned);
    put_u16(writer, game->prizes.discovered);
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
        put_u32(writer, game->prizes.origin_pet[i]);
    put_u8(writer, game->prizes.offered);
    put_u32(writer, game->prizes.offered_pet);
    put_u16(writer, game->new_pets);
    for (unsigned i = 0u; i < game->count; ++i) {
        put_u8(writer, game->pets[i].collection_entry);
        put_u8(writer, (uint8_t)(game->pets[i].reached_forms | (1u << game->pets[i].form)));
    }
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
    if (reader->version >= 3u) {
        read_sleep_log(reader, &game->sleep_log);
        game->timezone_minutes = get_i16(reader);
        game->clock_adjust = get_i16(reader);
        game->prizes.owned = get_u16(reader);
        game->prizes.discovered = get_u16(reader);
        for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
            game->prizes.origin_pet[i] = get_u32(reader);
        game->prizes.offered = get_u8(reader);
        game->prizes.offered_pet = get_u32(reader);
        if (game->prizes.owned > JELLI_PRIZE_MASK || game->prizes.discovered > JELLI_PRIZE_MASK ||
            game->prizes.offered > JELLI_PRIZE_COUNT)
            reader->failed = true;
    }
    if (reader->version >= 4u) {
        game->new_pets = get_u16(reader);
        for (unsigned i = 0u; i < game->count; ++i) {
            game->pets[i].collection_entry = get_u8(reader);
            game->pets[i].reached_forms = get_u8(reader);
        }
    } else {
        jelli_collection_migrate(game);
    }
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
           bytes[4] >= 1u && bytes[4] <= SAVE_VERSION && bytes[5] == 0u &&
           bytes[6] == SAVE_HEADER_SIZE && bytes[7] == 0u;
}

bool jelli_save_decode_workspace(JelliSave *save, const uint8_t *bytes, size_t size,
                                 JelliSave *candidate)
{
    if (save == NULL || candidate == NULL || save == candidate || bytes == NULL ||
        size > JELLI_SAVE_CAPACITY || !header_valid(bytes, size))
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
    memset(candidate, 0, sizeof(*candidate));
    reader.offset = 12u;
    candidate->sequence = get_u64(&reader);
    candidate->anchor_ms = get_u64(&reader);
    (void)read_bool(&reader, &candidate->anchor_valid);
    if (get_u8(&reader) != SAVE_CONTENT_VERSION || get_u8(&reader) != 0u || get_u8(&reader) != 0u)
        reader.failed = true;
    (void)read_game(&reader, &candidate->game);
    if (reader.failed || reader.offset != reader.size || candidate->game.resuming ||
        candidate->game.resume_remaining_ms != 0u || candidate->game.backlog_ms >= 100u ||
        !jelli_game_valid(&candidate->game))
        return false;
    if (reader.version < 4u)
        jelli_collection_unlock(&candidate->game);
    *save = *candidate;
    return true;
}

bool jelli_save_decode(JelliSave *save, const uint8_t *bytes, size_t size)
{
    JelliSave candidate;
    return jelli_save_decode_workspace(save, bytes, size, &candidate);
}

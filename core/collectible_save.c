#include "collectible_save.h"

static void write_chain(Writer *writer, const JelliChain *chain)
{
    for (unsigned i = 0u; i < JELLI_CHAIN_LENGTH; ++i)
        put_u8(writer, chain->body[i]);
    put_u8(writer, chain->length);
    put_u8(writer, chain->direction);
    put_u8(writer, chain->pending_direction);
    put_u8(writer, chain->pickup);
    put_u8(writer, chain->status);
    put_u8(writer, chain->step_ticks);
    put_u8(writer, chain->turned ? 1u : 0u);
    put_u8(writer, chain->relaxed ? 1u : 0u);
    put_u8(writer, chain->buffered_direction);
}

static bool read_bool(Reader *reader)
{
    unsigned value = get_u8(reader);
    if (value > 1u)
        reader->failed = true;
    return value == 1u;
}

static void read_chain(Reader *reader, JelliChain *chain, unsigned schema)
{
    for (unsigned i = 0u; i < JELLI_CHAIN_LENGTH; ++i)
        chain->body[i] = get_u8(reader);
    chain->length = get_u8(reader);
    chain->direction = get_u8(reader);
    chain->pending_direction = get_u8(reader);
    chain->pickup = get_u8(reader);
    chain->status = get_u8(reader);
    chain->step_ticks = get_u8(reader);
    chain->turned = read_bool(reader);
    chain->relaxed = read_bool(reader);
    chain->buffered_direction = schema >= 2u ? get_u8(reader) : 0u;
}

void jelli_collectible_save_write(Writer *writer, const JelliGame *game)
{
    put_u8(writer, 2u); /* Schema2 adds one buffered direction byte. */
    for (unsigned i = 0u; i < game->count; ++i) {
        const JelliCollectibleProgress *p = &game->pets[i].collectible_progress;
        for (unsigned j = 0u; j < JELLI_PRIZE_V16_COUNT; ++j)
            put_u16(writer, p->play_ticks[j]);
        put_u16(writer, p->special_unlocked);
        put_u8(writer, p->record_rewarded & 1u);
    }
    const JelliCollectibleSession *s = &game->collectible;
    put_u32(writer, s->pet_id);
    put_u32(writer, s->record_cursor);
    put_u32(writer, s->generation);
    put_u16(writer, s->elapsed_ticks);
    put_u8(writer, s->prize);
    put_u8(writer, s->mode);
    put_u8(writer, s->active ? 1u : 0u);
    put_u8(writer, s->paused ? 1u : 0u);
    write_chain(writer, &s->chain);
    put_u16(writer, (uint16_t)(game->prizes.owned >> 16u));
    put_u16(writer, (uint16_t)(game->prizes.discovered >> 16u));
    for (unsigned i = JELLI_PRIZE_V16_COUNT; i < JELLI_PRIZE_COUNT; ++i)
        put_u32(writer, game->prizes.origin_pet[i]);
    for (unsigned i = 0u; i < game->count; ++i) {
        for (unsigned j = JELLI_PRIZE_V16_COUNT; j < JELLI_PRIZE_COUNT; ++j)
            put_u16(writer, game->pets[i].collectible_progress.play_ticks[j]);
        put_u8(writer, game->pets[i].collectible_progress.record_rewarded);
        for (unsigned j = 0u; j < 7u; ++j)
            put_u16(writer, game->pets[i].moment_rewards[j]);
        put_u8(writer, game->pets[i].moment_pulse);
    }
    jelli_bubble_save_write(writer, game);
    put_u16(writer, game->tunes_seen);
}

void jelli_collectible_save_read(Reader *reader, JelliGame *game)
{
    if (reader->version < 16u) {
        /* Historical ownership/discovery remain intact; new progress starts empty. */
        return;
    }
    unsigned schema = get_u8(reader);
    if (schema < 1u || schema > 2u)
        reader->failed = true;
    for (unsigned i = 0u; i < game->count; ++i) {
        JelliCollectibleProgress *p = &game->pets[i].collectible_progress;
        for (unsigned j = 0u; j < JELLI_PRIZE_V16_COUNT; ++j)
            p->play_ticks[j] = get_u16(reader);
        p->special_unlocked = get_u16(reader);
        p->record_rewarded = read_bool(reader);
    }
    JelliCollectibleSession *s = &game->collectible;
    s->pet_id = get_u32(reader);
    s->record_cursor = get_u32(reader);
    s->generation = get_u32(reader);
    s->elapsed_ticks = get_u16(reader);
    s->prize = get_u8(reader);
    s->mode = get_u8(reader);
    s->active = read_bool(reader);
    s->paused = read_bool(reader);
    read_chain(reader, &s->chain, schema);
    if (reader->version >= 17u) {
        game->prizes.owned |= (uint32_t)get_u16(reader) << 16u;
        game->prizes.discovered |= (uint32_t)get_u16(reader) << 16u;
        for (unsigned i = JELLI_PRIZE_V16_COUNT; i < JELLI_PRIZE_COUNT; ++i)
            game->prizes.origin_pet[i] = get_u32(reader);
        for (unsigned i = 0u; i < game->count; ++i) {
            for (unsigned j = JELLI_PRIZE_V16_COUNT; j < JELLI_PRIZE_COUNT; ++j)
                game->pets[i].collectible_progress.play_ticks[j] = get_u16(reader);
            game->pets[i].collectible_progress.record_rewarded = get_u8(reader);
            for (unsigned j = 0u; j < 7u; ++j)
                game->pets[i].moment_rewards[j] = get_u16(reader);
            game->pets[i].moment_pulse = get_u8(reader);
        }
    }
    jelli_bubble_save_read(reader, game);
    if (reader->version >= 19u)
        game->tunes_seen = get_u16(reader);
    /* Validation must inspect raw state before normalizing resume policy. */
}

#include "collectible_save.h"

void jelli_bubble_save_write(Writer *writer, const JelliGame *game)
{
    const JelliBubble *lab = &game->collectible.bubble;
    put_u8(writer, 3u); /* Content/codec identity; unknown future layouts are preserved. */
    for (unsigned i = 0u; i < JELLI_BUBBLE_CELLS; i += 2u)
        put_u8(writer, (uint8_t)(lab->cells[i] | (lab->cells[i + 1u] << 4u)));
    put_u16(writer, lab->score);
    put_u16(writer, lab->level_score);
    put_u8(writer, lab->level);
    put_u8(writer, lab->cursor);
    put_u8(writer, lab->x);
    put_u8(writer, lab->y);
    put_u8(writer, lab->vertical);
    put_u8(writer, lab->color);
    put_u8(writer, lab->status);
    put_u8(writer, lab->ticks);
    put_u8(writer, lab->assist);
    for (unsigned i = 0u; i < JELLI_CARTRIDGE_COUNT; ++i) {
        put_u16(writer, game->cartridge_stats[i].plays);
        put_u16(writer, game->cartridge_stats[i].wins);
    }
    put_u8(writer, game->activity_music_enabled ? 1u : 0u);
}

void jelli_bubble_save_read(Reader *reader, JelliGame *game)
{
    if (reader->version < 18u)
        return;
    JelliBubble *lab = &game->collectible.bubble;
    unsigned schema = get_u8(reader);
    if (schema < 1u || schema > 3u)
        reader->failed = true;
    for (unsigned i = 0u; i < JELLI_BUBBLE_CELLS; i += 2u) {
        unsigned pair = get_u8(reader);
        lab->cells[i] = (uint8_t)(pair & 15u);
        lab->cells[i + 1u] = (uint8_t)(pair >> 4u);
    }
    lab->score = get_u16(reader);
    lab->level_score = get_u16(reader);
    lab->level = get_u8(reader);
    lab->cursor = get_u8(reader);
    lab->x = get_u8(reader);
    lab->y = get_u8(reader);
    lab->vertical = get_u8(reader);
    lab->color = get_u8(reader);
    lab->status = get_u8(reader);
    lab->ticks = get_u8(reader);
    lab->assist = get_u8(reader);
    if (schema >= 2u)
        for (unsigned i = 0u; i < JELLI_CARTRIDGE_COUNT; ++i) {
            game->cartridge_stats[i].plays = get_u16(reader);
            game->cartridge_stats[i].wins = get_u16(reader);
        }
    if (schema >= 3u) {
        unsigned enabled = get_u8(reader);
        if (enabled > 1u)
            reader->failed = true;
        game->activity_music_enabled = enabled == 1u;
    }
}

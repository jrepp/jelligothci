#include "save_tail.h"

void jelli_save_write_tail(Writer *writer, const JelliGame *game)
{
    for (unsigned i = 0u; i < game->count; ++i) {
        const JelliPet *pet = &game->pets[i];
        put_u32(writer, pet->rest_ticks);
        put_u8(writer, pet->moment); /* Version 10: moments and potty cycle. */
        put_u16(writer, pet->digesting);
        put_u16(writer, pet->potty);
        put_u8(writer, pet->behavior); /* Version 11: behaviour state (RFC-005). */
        put_u16(writer, pet->behavior_left);
        put_u8(writer, pet->cooldown_state);
        put_u16(writer, pet->cooldown_left);
        put_u8(writer, pet->low_needs);
    }
}

void jelli_save_read_tail(Reader *reader, JelliGame *game)
{
    for (unsigned i = 0u; i < game->count && reader->version >= 9u; ++i) {
        JelliPet *pet = &game->pets[i];
        pet->rest_ticks = get_u32(reader);
        if (reader->version < 10u)
            continue;
        pet->moment = get_u8(reader);
        pet->digesting = get_u16(reader);
        pet->potty = get_u16(reader);
        if (reader->version < 11u)
            continue;
        pet->behavior = get_u8(reader);
        pet->behavior_left = get_u16(reader);
        pet->cooldown_state = get_u8(reader);
        pet->cooldown_left = get_u16(reader);
        pet->low_needs = get_u8(reader);
    }
}

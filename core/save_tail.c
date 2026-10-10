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
        put_u8(writer, pet->behavior_flags);
        put_u16(writer, pet->activity_day);
        put_u32(writer, pet->completed_moments);
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
        pet->behavior_flags = get_u8(reader);
        if (reader->version >= 12u) {
            pet->activity_day = get_u16(reader);
            pet->completed_moments = get_u32(reader);
        }
    }
    /* v13 pays recipe costs at start and rewards at completion. Old in-flight
     * recipes may already have granted rewards; retire them without replay. */
    for (unsigned i = 0u; i < game->count && reader->version < 13u; ++i) {
        JelliPet *pet = &game->pets[i];
        if (pet->moment && (pet->activity == JELLI_EATING || pet->activity == JELLI_PLAYING)) {
            pet->moment = 0u;
            pet->activity = JELLI_IDLE;
            pet->interaction_due = 0u;
        }
    }
}

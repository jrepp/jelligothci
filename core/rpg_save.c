#include "rpg_save.h"
#include "jelli/activities.h"

void jelli_rpg_save_write(Writer *writer, const JelliGame *game)
{
    for (unsigned i = 0u; i < game->count; ++i)
        for (unsigned stat = 0u; stat < JELLI_RPG_STAT_COUNT; ++stat)
            put_u16(writer, game->pets[i].rpg_stats[stat]);
}

void jelli_rpg_save_read(Reader *reader, JelliGame *game)
{
    if (reader->version < 22u)
        return;
    for (unsigned i = 0u; i < game->count; ++i)
        for (unsigned stat = 0u; stat < JELLI_RPG_STAT_COUNT; ++stat)
            game->pets[i].rpg_stats[stat] = get_u16(reader);
}

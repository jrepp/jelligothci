#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool scope(const JelliGame *game, uint32_t id, uint8_t *form)
{
    *form = 0;
    if (!id)
        return true;
    for (unsigned i = 0; i < game->count; ++i)
        if (game->pets[i].id == id) {
            *form = game->pets[i].form;
            return true;
        }
    return false;
}

static void list(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id, uint32_t pet,
                 uint8_t form)
{
    int size = snprintf(debug->reply, sizeof(debug->reply),
                        "\n@J1 %" PRIu32 " {\"ok\":true,\"creature\":%" PRIu32
                        ",\"session_only\":true,\"tunables\":[",
                        id, pet);
    if (size < 0 || (size_t)size >= sizeof(debug->reply))
        return;
    size_t used = (size_t)size;
    for (unsigned i = 0; i < JELLI_TUNE_COUNT; ++i) {
        JelliTunable key = (JelliTunable)i;
        const JelliTunableDefinition *d = jelli_tunable_definition(key);
        size =
            snprintf(debug->reply + used, sizeof(debug->reply) - used,
                     "%s{\"name\":\"%s\",\"value\":%" PRIu32 ",\"default\":%" PRIu32
                     ",\"min\":%" PRIu32 ",\"max\":%" PRIu32 ",\"unit\":\"%s\",\"source\":\"%s\"}",
                     i ? "," : "", d->name, jelli_tunable_get(&engine->ui.tunables, pet, form, key),
                     d->initial, d->minimum, d->maximum, d->unit,
                     jelli_tunable_source(&engine->ui.tunables, pet, form, key));
        if (size < 0 || (size_t)size >= sizeof(debug->reply) - used)
            return;
        used += (size_t)size;
    }
    if (used + 4u > sizeof(debug->reply))
        return;
    memcpy(debug->reply + used, "]}\n", 4u);
    debug->reply_size = used + 3u;
}

void jelli_debug_tunables(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                          unsigned count)
{
    uint32_t pet = 0, value = 0;
    uint8_t form;
    if (count < 4u || !jelli_debug_number(words[3], &pet) || !scope(&engine->game, pet, &form)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"invalid_scope\"}");
        return;
    }
    if (!strcmp(words[2], "tunables") && count == 4u) {
        list(debug, engine, id, pet, form);
        return;
    }
    if (debug->captured || engine->game.resuming) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    JelliTunable key;
    if (count != 6u || strcmp(words[2], "tune") != 0 || !jelli_tunable_find(words[4], &key)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"unknown_tunable\"}");
        return;
    }
    bool reset = !strcmp(words[5], "reset");
    bool ok = reset ? jelli_tunable_reset(&engine->ui.tunables, pet, key)
                    : jelli_debug_number(words[5], &value) &&
                          jelli_tunable_set(&engine->ui.tunables, pet, key, value);
    if (!ok) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"tunable_range\"}");
        return;
    }
    list(debug, engine, id, pet, form);
}

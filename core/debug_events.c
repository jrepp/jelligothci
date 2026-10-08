#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>

static int snapshot(char *out, size_t capacity, const JelliEventSnapshot *v)
{
    return snprintf(out, capacity,
                    "{\"needs\":[%u,%u,%u,%u,%u],\"bond\":%u,\"food\":%u,\"gifts\":%u,"
                    "\"location\":%u,\"health\":%u,\"activity\":%u,\"flags\":%u}",
                    v->needs[0], v->needs[1], v->needs[2], v->needs[3], v->needs[4], v->bond,
                    v->food, v->gifts, v->location, v->health, v->activity, v->flags);
}

static size_t event_json(char *out, size_t capacity, const JelliEvent *e, bool comma)
{
    int size = snprintf(out, capacity,
                        "%s{\"seq\":%" PRIu32 ",\"tick\":%" PRIu64 ",\"pet\":%" PRIu32
                        ",\"kind\":%u,\"code\":%u,\"input\":%u,\"result\":%u,\"value\":%" PRIu32
                        ",\"before\":",
                        comma ? "," : "", e->sequence, e->tick, e->pet_id, e->kind, e->code,
                        e->input, e->result, e->value);
    if (size < 0 || (size_t)size >= capacity)
        return 0;
    size_t used = (size_t)size;
    size = snapshot(out + used, capacity - used, &e->before);
    if (size < 0 || (size_t)size + 10u >= capacity - used)
        return 0;
    used += (size_t)size;
    const char separator[] = ",\"after\":";
    for (size_t i = 0; i < sizeof(separator) - 1u; ++i)
        out[used++] = separator[i];
    size = snapshot(out + used, capacity - used, &e->after);
    if (size < 0 || (size_t)size + 2u >= capacity - used)
        return 0;
    used += (size_t)size;
    out[used++] = '}';
    out[used] = '\0';
    return used;
}

void jelli_debug_events(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id, char **words,
                        unsigned count)
{
    uint32_t after = 0;
    if (count != 4u || !jelli_debug_number(words[3], &after)) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"event_cursor\"}");
        return;
    }
    const JelliEventLog *log = &engine->events;
    const JelliEvent *first = jelli_events_at(log, 0u);
    bool reset = after > log->sequence;
    bool dropped = !reset && after && first && after < first->sequence - 1u;
    if (reset)
        after = 0u;
    int size = snprintf(debug->reply, sizeof(debug->reply),
                        "\n@J1 %" PRIu32 " {\"ok\":true,\"latest\":%" PRIu32
                        ",\"reset\":%s,\"dropped\":%s,\"events\":[",
                        id, log->sequence, reset ? "true" : "false", dropped ? "true" : "false");
    if (size < 0 || (size_t)size >= sizeof(debug->reply))
        return;
    size_t used = (size_t)size;
    unsigned emitted = 0;
    uint32_t next = after;
    for (unsigned i = 0; i < log->count && emitted < 4u; ++i) {
        const JelliEvent *e = jelli_events_at(log, i);
        if (!e || e->sequence <= after)
            continue;
        size_t added =
            event_json(debug->reply + used, sizeof(debug->reply) - used, e, emitted > 0u);
        if (!added) {
            jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"event_capacity\"}");
            return;
        }
        used += added;
        next = e->sequence;
        ++emitted;
    }
    size = snprintf(debug->reply + used, sizeof(debug->reply) - used,
                    "],\"next\":%" PRIu32 ",\"more\":%s}\n", next,
                    next < log->sequence ? "true" : "false");
    if (size > 0 && (size_t)size < sizeof(debug->reply) - used)
        debug->reply_size = used + (size_t)size;
}

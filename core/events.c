#include "jelli/events.h"
#include <stddef.h>

_Static_assert(sizeof(JelliEvent) == 64u, "Event record exceeds 64 bytes");
_Static_assert(sizeof(JelliEventLog) <= 2064u, "Event history exceeds budget");

void jelli_events_push(JelliEventLog *log, JelliEvent event)
{
    if (!log)
        return;
    /* A sequence rollover starts a fresh history; readers see a cursor reset. */
    if (log->sequence == UINT32_MAX) {
        log->sequence = 0u;
        log->head = log->count = 0u;
    }
    event.sequence = ++log->sequence;
    event.input = log->input;
    log->items[log->head] = event;
    log->head = (uint8_t)((log->head + 1u) % JELLI_EVENT_CAPACITY);
    if (log->count < JELLI_EVENT_CAPACITY)
        ++log->count;
}

const JelliEvent *jelli_events_at(const JelliEventLog *log, unsigned oldest_index)
{
    if (!log || oldest_index >= log->count)
        return NULL;
    unsigned index =
        (log->head + JELLI_EVENT_CAPACITY - log->count + oldest_index) % JELLI_EVENT_CAPACITY;
    return &log->items[index];
}

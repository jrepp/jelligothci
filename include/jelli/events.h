#ifndef JELLI_EVENTS_H
#define JELLI_EVENTS_H
#include <stdint.h>

#define JELLI_EVENT_CAPACITY 32u
/* No frame/animation/ordinary need-decay events. History is transient, caller
 * owned, engine-thread-only, and never serialized into a pet save. */
typedef enum {
    JELLI_EVENT_COMMAND,
    JELLI_EVENT_INPUT,
    JELLI_EVENT_EFFECT,
    JELLI_EVENT_STATUS,
    JELLI_EVENT_CHEAT
} JelliEventKind;
typedef struct {
    uint16_t needs[5], bond, food, gifts;
    uint8_t location, health, activity, flags; /* asleep=1, reward pending=2, claimed=4 */
} JelliEventSnapshot;
typedef struct {
    uint64_t tick;
    uint32_t sequence, pet_id, value;
    JelliEventSnapshot before, after;
    uint8_t kind, code, result, input; /* input: UI action + 1, zero for non-UI */
} JelliEvent;
typedef struct {
    JelliEvent items[JELLI_EVENT_CAPACITY];
    uint32_t sequence;
    uint8_t head, count, input;
} JelliEventLog;

void jelli_events_push(JelliEventLog *log, JelliEvent event);
const JelliEvent *jelli_events_at(const JelliEventLog *log, unsigned oldest_index);
#endif

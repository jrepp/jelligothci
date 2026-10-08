#ifndef JELLI_SLEEP_LOG_H
#define JELLI_SLEEP_LOG_H

#include <stdbool.h>
#include <stdint.h>

#define JELLI_SLEEP_SESSION_CAPACITY 8u
#define JELLI_SLEEP_BED_KNOWN 1u
#define JELLI_SLEEP_WAKE_KNOWN 2u
#define JELLI_SLEEP_REAL_DURATION 4u
#define JELLI_SLEEP_STATS_KNOWN 8u

typedef struct {
    uint64_t bed_unix_seconds, wake_unix_seconds, start_tick;
    uint32_t duration_seconds;
    uint8_t flags;
    uint16_t bed_energy, bed_sleep_score;
} JelliSleepSession;

typedef struct {
    JelliSleepSession sessions[JELLI_SLEEP_SESSION_CAPACITY];
    uint64_t total_seconds;
    uint8_t head, count;
    bool active;
    uint32_t pet_id;
} JelliSleepLog;

/* A manual linked bedtime/wake journal. Automatic pet naps never call begin.
 * Wall timestamps are optional injected observations, never synthesized. */
bool jelli_sleep_log_valid(const JelliSleepLog *log);
bool jelli_sleep_log_begin(JelliSleepLog *log, uint32_t pet_id, uint64_t start_tick,
                           bool wall_known, uint64_t wall_seconds);
bool jelli_sleep_log_finish(JelliSleepLog *log, uint64_t end_tick, bool wall_known,
                            uint64_t wall_seconds);

#endif

#ifndef JELLI_POWER_LOG_H
#define JELLI_POWER_LOG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JELLI_POWER_LOG_RECORD_BYTES 40u
#define JELLI_POWER_LOG_CAPACITY 32u

enum {
    JELLI_POWER_BATTERY_PRESENT = 1u,
    JELLI_POWER_VOLTAGE_VALID = 2u,
    JELLI_POWER_GAUGE_VALID = 4u,
    JELLI_POWER_WALL_VALID = 8u,
    JELLI_POWER_DISPLAY_BLANK = 16u,
    JELLI_POWER_PET_ASLEEP = 32u,
    JELLI_POWER_READING_VALID = 64u
};
enum {
    JELLI_POWER_BOOT = 1u,
    JELLI_POWER_PERIODIC = 2u,
    JELLI_POWER_SOURCE_CHANGE = 4u,
    JELLI_POWER_DISPLAY_CHANGE = 8u,
    JELLI_POWER_PET_SLEEP_CHANGE = 16u
};
typedef struct {
    uint64_t monotonic_ms, wall_seconds;
    uint32_t sequence, boot;
    uint16_t battery_mv;
    uint8_t battery_percent, status[2], flags, reasons, reset_reason;
} JelliPowerRecord;

/* Stable little-endian storage; unsupported or malformed data is rejected and
 * does not change the destination. No native struct padding is persisted. */
bool jelli_power_record_encode(const JelliPowerRecord *record, uint8_t *bytes, size_t capacity);
bool jelli_power_record_decode(JelliPowerRecord *record, const uint8_t *bytes, size_t size);
#endif

#include "jelli/power_log.h"

static void put(uint8_t *bytes, uint64_t value, unsigned count)
{
    for (unsigned i = 0u; i < count; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8u));
}
static uint64_t get(const uint8_t *bytes, unsigned count)
{
    uint64_t value = 0u;
    for (unsigned i = 0u; i < count; ++i)
        value |= (uint64_t)bytes[i] << (i * 8u);
    return value;
}
static bool valid(const JelliPowerRecord *record)
{
    return record->sequence && record->reasons && !(record->reasons & 0xe0u) &&
           !(record->flags & 0x80u) &&
           (!(record->flags & JELLI_POWER_GAUGE_VALID) || record->battery_percent <= 100u) &&
           (!(record->flags & (JELLI_POWER_VOLTAGE_VALID | JELLI_POWER_GAUGE_VALID)) ||
            ((record->flags & (JELLI_POWER_READING_VALID | JELLI_POWER_BATTERY_PRESENT)) ==
             (JELLI_POWER_READING_VALID | JELLI_POWER_BATTERY_PRESENT)));
}

bool jelli_power_record_encode(const JelliPowerRecord *record, uint8_t *bytes, size_t capacity)
{
    if (!record || !bytes || capacity < JELLI_POWER_LOG_RECORD_BYTES || !valid(record))
        return false;
    for (unsigned i = 0u; i < JELLI_POWER_LOG_RECORD_BYTES; ++i)
        bytes[i] = 0u;
    bytes[0] = 'J';
    bytes[1] = 'P';
    bytes[2] = 'L';
    bytes[3] = 1u;
    put(bytes + 4, record->sequence, 4);
    put(bytes + 8, record->boot, 4);
    put(bytes + 12, record->monotonic_ms, 8);
    put(bytes + 20, record->wall_seconds, 8);
    put(bytes + 28, record->battery_mv, 2);
    bytes[30] = record->battery_percent;
    bytes[31] = record->status[0];
    bytes[32] = record->status[1];
    bytes[33] = record->flags;
    bytes[34] = record->reasons;
    bytes[35] = record->reset_reason;
    return true;
}

bool jelli_power_record_decode(JelliPowerRecord *record, const uint8_t *bytes, size_t size)
{
    if (!record || !bytes || size != JELLI_POWER_LOG_RECORD_BYTES || bytes[0] != 'J' ||
        bytes[1] != 'P' || bytes[2] != 'L' || bytes[3] != 1u || bytes[36] || bytes[37] ||
        bytes[38] || bytes[39])
        return false;
    JelliPowerRecord decoded = {.sequence = (uint32_t)get(bytes + 4, 4),
                                .boot = (uint32_t)get(bytes + 8, 4),
                                .monotonic_ms = get(bytes + 12, 8),
                                .wall_seconds = get(bytes + 20, 8),
                                .battery_mv = (uint16_t)get(bytes + 28, 2),
                                .battery_percent = bytes[30],
                                .status = {bytes[31], bytes[32]},
                                .flags = bytes[33],
                                .reasons = bytes[34],
                                .reset_reason = bytes[35]};
    if (!valid(&decoded))
        return false;
    *record = decoded;
    return true;
}

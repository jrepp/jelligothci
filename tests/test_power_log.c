#include "jelli/power_log.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    JelliPowerRecord input = {.sequence = 0x12345678u,
                              .boot = 9u,
                              .monotonic_ms = UINT64_C(123456789012),
                              .wall_seconds = UINT64_C(1760000000),
                              .battery_mv = 3575u,
                              .battery_percent = 10u,
                              .status = {0x38, 0x32},
                              .flags = JELLI_POWER_READING_VALID | JELLI_POWER_BATTERY_PRESENT |
                                       JELLI_POWER_VOLTAGE_VALID | JELLI_POWER_GAUGE_VALID |
                                       JELLI_POWER_WALL_VALID,
                              .reasons = JELLI_POWER_BOOT,
                              .reset_reason = 3u};
    uint8_t bytes[JELLI_POWER_LOG_RECORD_BYTES];
    CHECK(jelli_power_record_encode(&input, bytes, sizeof(bytes)));
    CHECK(bytes[4] == 0x78 && bytes[5] == 0x56 && bytes[28] == 0xf7 && bytes[29] == 0x0d);
    JelliPowerRecord output = {0};
    CHECK(jelli_power_record_decode(&output, bytes, sizeof(bytes)));
    CHECK(output.monotonic_ms == input.monotonic_ms && output.wall_seconds == input.wall_seconds &&
          output.sequence == input.sequence && output.battery_mv == input.battery_mv &&
          output.flags == input.flags && output.status[1] == input.status[1]);
    bytes[36] = 1;
    CHECK(!jelli_power_record_decode(&output, bytes, sizeof(bytes)));
    CHECK(output.sequence == input.sequence);
    bytes[36] = 0;
    bytes[30] = 101;
    CHECK(!jelli_power_record_decode(&output, bytes, sizeof(bytes)));
    CHECK(!jelli_power_record_decode(&output, bytes, sizeof(bytes) - 1));
    input.flags &= (uint8_t)~JELLI_POWER_BATTERY_PRESENT;
    CHECK(!jelli_power_record_encode(&input, bytes, sizeof(bytes)));
    return 0;
}

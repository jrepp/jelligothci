#ifndef JELLI_POWER_H
#define JELLI_POWER_H

#include <stdbool.h>
#include <stdint.h>

/* Host-observed supply state. The core never polls hardware: hosts write a
 * reading into the pet UI between frames on the engine thread. */
typedef enum {
    JELLI_POWER_UNKNOWN,    /* No reading yet, or the host could not read it. */
    JELLI_POWER_NO_BATTERY, /* External power only; no battery is present. */
    JELLI_POWER_DISCHARGING,
    JELLI_POWER_CHARGING,
    JELLI_POWER_CHARGED,
    JELLI_POWER_STATE_COUNT
} JelliPowerState;

#define JELLI_POWER_PERCENT_UNKNOWN 255u

typedef struct {
    uint8_t state;   /* JelliPowerState; other values read as UNKNOWN. */
    uint8_t percent; /* 0..100, or JELLI_POWER_PERCENT_UNKNOWN; 101..254 clamp to 100. */
} JelliPowerReading;

/* Stable artwork IDs 12001..12007 in assets/slice/assets.json. */
typedef enum {
    JELLI_BATTERY_EMPTY,  /* Below 10 percent: no bars and an exclamation mark. */
    JELLI_BATTERY_LOW,    /* 10..24 percent: one bar. */
    JELLI_BATTERY_MEDIUM, /* 25..69 percent: two bars. */
    JELLI_BATTERY_HIGH,   /* 70..100 percent, or charge complete: three bars. */
    JELLI_BATTERY_CHARGING,
    JELLI_BATTERY_PLUGGED,
    JELLI_BATTERY_UNKNOWN,
    JELLI_BATTERY_ICON_COUNT
} JelliBatteryIcon;

#define JELLI_BATTERY_ASSET_BASE 12001u
#define JELLI_BATTERY_LOW_PERCENT 25u
#define JELLI_BATTERY_CAPTION 10u

typedef struct {
    uint8_t icon; /* JelliBatteryIcon; asset ID is JELLI_BATTERY_ASSET_BASE + icon. */
    uint8_t bars; /* Level bars drawn by the icon, 0..3; zero for non-level icons. */
    bool low;     /* Discharging below 25 percent: caption says LOW, not only a color. */
    char caption[JELLI_BATTERY_CAPTION]; /* NUL-terminated ASCII, at most seven glyphs. */
} JelliBatteryIndicator;

/* Canonical form: invalid states become UNKNOWN, out-of-range percentages clamp
 * to 100, and states whose display ignores the percentage report it as unknown.
 * Two readings with equal canonical forms always display identically. */
JelliPowerReading jelli_power_normalize(JelliPowerReading reading);

/* Pure mapping from any host reading to the icon and caption to draw. */
JelliBatteryIndicator jelli_battery_indicator(JelliPowerReading reading);

/* Stable lower-case protocol name, e.g. "charging"; NULL for invalid values. */
const char *jelli_power_state_name(unsigned state);

#endif

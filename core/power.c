#include "jelli/power.h"

#include <stddef.h>

static const char *const state_names[JELLI_POWER_STATE_COUNT] = {
    "unknown", "no-battery", "discharging", "charging", "charged"};

const char *jelli_power_state_name(unsigned state)
{
    return state < JELLI_POWER_STATE_COUNT ? state_names[state] : NULL;
}

JelliPowerReading jelli_power_normalize(JelliPowerReading reading)
{
    if (reading.state >= JELLI_POWER_STATE_COUNT)
        reading.state = JELLI_POWER_UNKNOWN;
    if (reading.percent > 100u && reading.percent != JELLI_POWER_PERCENT_UNKNOWN)
        reading.percent = 100u;
    /* These states draw fixed text, so their percentage must not cause redraws. */
    if (reading.state == JELLI_POWER_UNKNOWN || reading.state == JELLI_POWER_NO_BATTERY ||
        reading.state == JELLI_POWER_CHARGED)
        reading.percent = JELLI_POWER_PERCENT_UNKNOWN;
    return reading;
}

/* Copies a literal into the fixed caption; every literal here fits by construction. */
static void caption_text(JelliBatteryIndicator *indicator, const char *text)
{
    size_t i = 0u;
    for (; text[i] && i + 1u < sizeof(indicator->caption); ++i)
        indicator->caption[i] = text[i];
    indicator->caption[i] = '\0';
}

/* "[LOW ]N%" for N in 0..100; at most eight glyphs plus the terminator. */
static void caption_percent(JelliBatteryIndicator *indicator, unsigned percent, bool low)
{
    char digits[3];
    unsigned count = 0u;
    do {
        digits[count++] = (char)('0' + percent % 10u);
        percent /= 10u;
    } while (percent && count < sizeof(digits));
    size_t used = 0u;
    if (low) {
        caption_text(indicator, "LOW ");
        used = 4u;
    }
    while (count)
        indicator->caption[used++] = digits[--count];
    indicator->caption[used++] = '%';
    indicator->caption[used] = '\0';
}

static JelliBatteryIndicator level(unsigned percent)
{
    JelliBatteryIndicator indicator = {.icon = JELLI_BATTERY_HIGH, .bars = 3u};
    if (percent < 10u)
        indicator = (JelliBatteryIndicator){.icon = JELLI_BATTERY_EMPTY, .bars = 0u};
    else if (percent < JELLI_BATTERY_LOW_PERCENT)
        indicator = (JelliBatteryIndicator){.icon = JELLI_BATTERY_LOW, .bars = 1u};
    else if (percent < 70u)
        indicator = (JelliBatteryIndicator){.icon = JELLI_BATTERY_MEDIUM, .bars = 2u};
    indicator.low = percent < JELLI_BATTERY_LOW_PERCENT;
    caption_percent(&indicator, percent, indicator.low);
    return indicator;
}

JelliBatteryIndicator jelli_battery_indicator(JelliPowerReading reading)
{
    reading = jelli_power_normalize(reading);
    bool known = reading.percent != JELLI_POWER_PERCENT_UNKNOWN;
    JelliBatteryIndicator indicator = {.icon = JELLI_BATTERY_UNKNOWN};
    switch ((JelliPowerState)reading.state) {
    case JELLI_POWER_NO_BATTERY:
        indicator.icon = JELLI_BATTERY_PLUGGED;
        caption_text(&indicator, "PLUGGED");
        return indicator;
    case JELLI_POWER_CHARGED:
        indicator = (JelliBatteryIndicator){.icon = JELLI_BATTERY_HIGH, .bars = 3u};
        caption_text(&indicator, "FULL");
        return indicator;
    case JELLI_POWER_CHARGING:
        indicator.icon = JELLI_BATTERY_CHARGING; /* The bolt says charging; text is the level. */
        if (!known)
            break;
        caption_percent(&indicator, reading.percent, false);
        return indicator;
    case JELLI_POWER_DISCHARGING:
        if (known)
            return level(reading.percent);
        break;
    case JELLI_POWER_UNKNOWN:
    case JELLI_POWER_STATE_COUNT:
        break;
    }
    caption_text(&indicator, "--%");
    return indicator;
}

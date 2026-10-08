#include "jelli/power.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static JelliBatteryIndicator show(unsigned state, unsigned percent)
{
    return jelli_battery_indicator((JelliPowerReading){(uint8_t)state, (uint8_t)percent});
}

static bool shows(unsigned state, unsigned percent, JelliBatteryIcon icon, unsigned bars, bool low,
                  const char *caption)
{
    JelliBatteryIndicator indicator = show(state, percent);
    return indicator.icon == icon && indicator.bars == bars && indicator.low == low &&
           strcmp(indicator.caption, caption) == 0;
}

static void level_boundaries(void)
{
    const unsigned d = JELLI_POWER_DISCHARGING;
    CHECK(shows(d, 0u, JELLI_BATTERY_EMPTY, 0u, true, "LOW 0%"));
    CHECK(shows(d, 9u, JELLI_BATTERY_EMPTY, 0u, true, "LOW 9%"));
    CHECK(shows(d, 10u, JELLI_BATTERY_LOW, 1u, true, "LOW 10%"));
    CHECK(shows(d, 24u, JELLI_BATTERY_LOW, 1u, true, "LOW 24%"));
    CHECK(shows(d, 25u, JELLI_BATTERY_MEDIUM, 2u, false, "25%"));
    CHECK(shows(d, 69u, JELLI_BATTERY_MEDIUM, 2u, false, "69%"));
    CHECK(shows(d, 70u, JELLI_BATTERY_HIGH, 3u, false, "70%"));
    CHECK(shows(d, 100u, JELLI_BATTERY_HIGH, 3u, false, "100%"));
    /* Bars rise monotonically with charge, so level never depends on color alone. */
    unsigned previous = 0u;
    for (unsigned percent = 0u; percent <= 100u; ++percent) {
        JelliBatteryIndicator indicator = show(d, percent);
        CHECK(indicator.bars >= previous && indicator.bars <= 3u);
        CHECK(indicator.low == (percent < JELLI_BATTERY_LOW_PERCENT));
        CHECK(strlen(indicator.caption) <= 7u);
        previous = indicator.bars;
    }
}

static void supply_states(void)
{
    CHECK(shows(JELLI_POWER_CHARGING, 42u, JELLI_BATTERY_CHARGING, 0u, false, "42%"));
    CHECK(shows(JELLI_POWER_CHARGING, 3u, JELLI_BATTERY_CHARGING, 0u, false, "3%"));
    CHECK(shows(JELLI_POWER_CHARGING, JELLI_POWER_PERCENT_UNKNOWN, JELLI_BATTERY_CHARGING, 0u,
                false, "--%"));
    CHECK(shows(JELLI_POWER_CHARGED, 97u, JELLI_BATTERY_HIGH, 3u, false, "FULL"));
    CHECK(shows(JELLI_POWER_CHARGED, JELLI_POWER_PERCENT_UNKNOWN, JELLI_BATTERY_HIGH, 3u, false,
                "FULL"));
    CHECK(shows(JELLI_POWER_NO_BATTERY, 0u, JELLI_BATTERY_PLUGGED, 0u, false, "PLUGGED"));
    CHECK(shows(JELLI_POWER_DISCHARGING, JELLI_POWER_PERCENT_UNKNOWN, JELLI_BATTERY_UNKNOWN, 0u,
                false, "--%"));
    /* Zero-initialized readings (no host sample yet) are unknown, never "0%". */
    CHECK(shows(JELLI_POWER_UNKNOWN, 0u, JELLI_BATTERY_UNKNOWN, 0u, false, "--%"));
    CHECK(shows(JELLI_POWER_UNKNOWN, 80u, JELLI_BATTERY_UNKNOWN, 0u, false, "--%"));
}

static void invalid_input(void)
{
    CHECK(shows(JELLI_POWER_STATE_COUNT, 50u, JELLI_BATTERY_UNKNOWN, 0u, false, "--%"));
    CHECK(shows(200u, 50u, JELLI_BATTERY_UNKNOWN, 0u, false, "--%"));
    CHECK(shows(JELLI_POWER_DISCHARGING, 101u, JELLI_BATTERY_HIGH, 3u, false, "100%"));
    CHECK(shows(JELLI_POWER_DISCHARGING, 254u, JELLI_BATTERY_HIGH, 3u, false, "100%"));
    CHECK(shows(JELLI_POWER_CHARGING, 180u, JELLI_BATTERY_CHARGING, 0u, false, "100%"));
    for (unsigned state = 0u; state < 256u; ++state) {
        for (unsigned percent = 0u; percent < 256u; ++percent) {
            JelliBatteryIndicator indicator = show(state, percent);
            CHECK(indicator.icon < JELLI_BATTERY_ICON_COUNT);
            CHECK(indicator.caption[0] && strlen(indicator.caption) <= 7u);
        }
    }
}

static void canonical_readings(void)
{
    JelliPowerReading a = jelli_power_normalize((JelliPowerReading){JELLI_POWER_CHARGED, 98u});
    JelliPowerReading b = jelli_power_normalize((JelliPowerReading){JELLI_POWER_CHARGED, 100u});
    CHECK(a.state == b.state && a.percent == b.percent && a.percent == JELLI_POWER_PERCENT_UNKNOWN);
    a = jelli_power_normalize((JelliPowerReading){JELLI_POWER_NO_BATTERY, 12u});
    CHECK(a.percent == JELLI_POWER_PERCENT_UNKNOWN);
    a = jelli_power_normalize((JelliPowerReading){9u, 40u});
    CHECK(a.state == JELLI_POWER_UNKNOWN && a.percent == JELLI_POWER_PERCENT_UNKNOWN);
    a = jelli_power_normalize((JelliPowerReading){JELLI_POWER_DISCHARGING, 140u});
    CHECK(a.state == JELLI_POWER_DISCHARGING && a.percent == 100u);
    a = jelli_power_normalize((JelliPowerReading){JELLI_POWER_DISCHARGING, 55u});
    CHECK(a.percent == 55u);
    /* Equal canonical forms must display identically for every input pair. */
    for (unsigned state = 0u; state < 8u; ++state) {
        for (unsigned percent = 0u; percent < 256u; ++percent) {
            JelliPowerReading raw = {(uint8_t)state, (uint8_t)percent};
            JelliBatteryIndicator direct = jelli_battery_indicator(raw);
            JelliBatteryIndicator again = jelli_battery_indicator(jelli_power_normalize(raw));
            CHECK(direct.icon == again.icon && direct.low == again.low &&
                  strcmp(direct.caption, again.caption) == 0);
        }
    }
}

/* Expected icon for a canonical reading, written independently of power.c. */
static unsigned expected_icon(unsigned state, unsigned percent)
{
    bool known = percent != JELLI_POWER_PERCENT_UNKNOWN;
    if (state == JELLI_POWER_NO_BATTERY)
        return JELLI_BATTERY_PLUGGED;
    if (state == JELLI_POWER_CHARGED)
        return JELLI_BATTERY_HIGH;
    if (state == JELLI_POWER_CHARGING)
        return JELLI_BATTERY_CHARGING;
    if (state != JELLI_POWER_DISCHARGING || !known)
        return JELLI_BATTERY_UNKNOWN;
    return percent < 10u   ? JELLI_BATTERY_EMPTY
           : percent < 25u ? JELLI_BATTERY_LOW
           : percent < 70u ? JELLI_BATTERY_MEDIUM
                           : JELLI_BATTERY_HIGH;
}

/* LOW wording exactly below 25 percent while discharging; level captions read back
 * as the clamped percentage (metamorphic), and fixed captions carry no number. */
static void check_level_caption(const JelliBatteryIndicator *a, JelliPowerReading canon,
                                unsigned percent)
{
    bool level = (canon.state == JELLI_POWER_DISCHARGING || canon.state == JELLI_POWER_CHARGING) &&
                 canon.percent != JELLI_POWER_PERCENT_UNKNOWN;
    bool low = level && canon.state == JELLI_POWER_DISCHARGING && canon.percent < 25u;
    CHECK(a->low == low && (strncmp(a->caption, "LOW ", 4u) == 0) == low);
    if (!level) {
        CHECK(strchr(a->caption, '%') == NULL || strcmp(a->caption, "--%") == 0);
        return;
    }
    unsigned shown = 0u;
    const char *digit = a->caption + (low ? 4u : 0u);
    for (; *digit >= '0' && *digit <= '9'; ++digit)
        shown = shown * 10u + (unsigned)(*digit - '0');
    CHECK(shown == (percent > 100u ? 100u : percent));
    CHECK(digit[0] == '%' && digit[1] == '\0');
}

/* Every state byte x every percent byte, including the 255 unknown sentinel. */
static void exhaustive_properties(void)
{
    static const unsigned bars[JELLI_BATTERY_ICON_COUNT] = {0u, 1u, 2u, 3u, 0u, 0u, 0u};
    for (unsigned state = 0u; state < 256u; ++state) {
        for (unsigned percent = 0u; percent < 256u; ++percent) {
            JelliPowerReading raw = {(uint8_t)state, (uint8_t)percent};
            JelliPowerReading canon = jelli_power_normalize(raw);
            JelliPowerReading twice = jelli_power_normalize(canon);
            CHECK(twice.state == canon.state && twice.percent == canon.percent); /* Idempotent. */
            CHECK(canon.state < JELLI_POWER_STATE_COUNT);
            CHECK(canon.percent <= 100u || canon.percent == JELLI_POWER_PERCENT_UNKNOWN);
            JelliBatteryIndicator a = jelli_battery_indicator(raw);
            JelliBatteryIndicator b = jelli_battery_indicator(raw);
            CHECK(memcmp(&a, &b, sizeof(a)) == 0); /* Deterministic, padding included. */
            CHECK(a.icon == expected_icon(canon.state, canon.percent));
            CHECK(a.bars == bars[a.icon]);
            size_t length = strlen(a.caption);
            CHECK(length >= 2u && length <= 7u); /* "5%" while charging .. "PLUGGED". */
            for (size_t i = 0; i < length; ++i)
                CHECK(a.caption[i] >= ' ' && a.caption[i] <= 'Z'); /* Upper-case font glyphs. */
            check_level_caption(&a, canon, percent);
        }
    }
    for (unsigned state = 0u; state < 256u; ++state)
        CHECK((jelli_power_state_name(state) != NULL) == (state < JELLI_POWER_STATE_COUNT));
}

static void protocol_names(void)
{
    CHECK(strcmp(jelli_power_state_name(JELLI_POWER_UNKNOWN), "unknown") == 0);
    CHECK(strcmp(jelli_power_state_name(JELLI_POWER_NO_BATTERY), "no-battery") == 0);
    CHECK(strcmp(jelli_power_state_name(JELLI_POWER_DISCHARGING), "discharging") == 0);
    CHECK(strcmp(jelli_power_state_name(JELLI_POWER_CHARGING), "charging") == 0);
    CHECK(strcmp(jelli_power_state_name(JELLI_POWER_CHARGED), "charged") == 0);
    CHECK(jelli_power_state_name(JELLI_POWER_STATE_COUNT) == NULL);
}

int main(void)
{
    level_boundaries();
    supply_states();
    invalid_input();
    canonical_readings();
    exhaustive_properties();
    protocol_names();
    puts("PASS: battery icon/caption mapping, boundaries, clamping, and canonical readings");
    return 0;
}

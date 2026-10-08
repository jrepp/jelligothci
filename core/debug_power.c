#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Writes "null" for an unknown percentage, otherwise its decimal value. */
static const char *percent_text(char out[4], JelliPowerReading reading)
{
    if (reading.percent == JELLI_POWER_PERCENT_UNKNOWN)
        return "null";
    (void)snprintf(out, 4u, "%u", (unsigned)reading.percent);
    return out;
}

size_t jelli_debug_battery_json(char *out, size_t capacity, const JelliPetEngine *engine)
{
    JelliPowerReading effective = jelli_power_normalize(jelli_pet_power(&engine->ui));
    JelliPowerReading host = jelli_power_normalize(engine->ui.power);
    JelliBatteryIndicator indicator = jelli_battery_indicator(effective);
    char percent[4], host_percent[4];
    int size =
        snprintf(out, capacity,
                 "{\"display\":%s,\"shown\":%s,\"state\":\"%s\",\"percent\":%s,\"override\":%s,"
                 "\"icon\":%u,\"caption\":\"%s\",\"host\":{\"state\":\"%s\",\"percent\":%s}}",
                 engine->game.battery_display ? "true" : "false",
                 engine->ui.last_view.battery_shown ? "true" : "false",
                 jelli_power_state_name(effective.state), percent_text(percent, effective),
                 engine->ui.power_override_active ? "true" : "false",
                 JELLI_BATTERY_ASSET_BASE + indicator.icon, indicator.caption,
                 jelli_power_state_name(host.state), percent_text(host_percent, host));
    return size < 0 || (size_t)size >= capacity ? 0u : (size_t)size;
}

/* STATE PERCENT, where PERCENT is 0..100 or "unknown". */
static bool parse_override(char **words, JelliPowerReading *reading)
{
    uint32_t percent = JELLI_POWER_PERCENT_UNKNOWN;
    if (strcmp(words[4], "unknown") != 0 &&
        (!jelli_debug_number(words[4], &percent) || percent > 100u))
        return false;
    for (unsigned state = 0u; state < JELLI_POWER_STATE_COUNT; ++state) {
        if (strcmp(words[3], jelli_power_state_name(state)) == 0) {
            *reading = (JelliPowerReading){(uint8_t)state, (uint8_t)percent};
            return true;
        }
    }
    return false;
}

/* Session-only override for previewing the indicator without a battery. It
 * never reaches the save file or the host, and "clear" restores host readings. */
void jelli_debug_battery(JelliDebug *debug, JelliPetEngine *engine, uint32_t id, char **words,
                         unsigned count)
{
    JelliPowerReading reading = {0u, 0u};
    bool clear = count == 4u && strcmp(words[3], "clear") == 0;
    if (count != 3u && !clear && (count != 5u || !parse_override(words, &reading))) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"battery_state_or_percent\"}");
        return;
    }
    if (count != 3u && debug->captured) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    if (clear)
        engine->ui.power_override_active = false;
    else if (count == 5u) {
        engine->ui.power_override = reading;
        engine->ui.power_override_active = true;
    }
    char body[320];
    memcpy(body, "{\"ok\":true,\"battery\":", 21u);
    size_t used = jelli_debug_battery_json(body + 21u, sizeof(body) - 22u, engine);
    if (!used) {
        jelli_debug_response(debug, id, "{\"ok\":false,\"error\":\"capacity\"}");
        return;
    }
    body[21u + used] = '}';
    body[22u + used] = '\0';
    jelli_debug_response(debug, id, body);
}

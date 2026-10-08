#include "power.h"
#include <SDL.h>

static JelliPowerState power_state(SDL_PowerState state)
{
    switch (state) {
    case SDL_POWERSTATE_ON_BATTERY:
        return JELLI_POWER_DISCHARGING;
    case SDL_POWERSTATE_NO_BATTERY: /* Desktops without a battery, such as a Mac mini. */
        return JELLI_POWER_NO_BATTERY;
    case SDL_POWERSTATE_CHARGING:
        return JELLI_POWER_CHARGING;
    case SDL_POWERSTATE_CHARGED:
        return JELLI_POWER_CHARGED;
    case SDL_POWERSTATE_UNKNOWN:
        break;
    }
    return JELLI_POWER_UNKNOWN;
}

void jelli_sdl_power_poll(JelliSdlPower *power, JelliPetEngine *engine, bool enabled)
{
    if (!power || !engine || !enabled)
        return;
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    if (power->sampled && now >= power->last_ms &&
        now - power->last_ms < JELLI_SDL_POWER_INTERVAL_MS)
        return;
    power->sampled = true;
    power->last_ms = now;
    int percent = -1; /* SDL reports -1 when the level cannot be determined. */
    SDL_PowerState state = SDL_GetPowerInfo(NULL, &percent);
    uint8_t level = JELLI_POWER_PERCENT_UNKNOWN;
    if (percent >= 0)
        level = (uint8_t)(percent > 100 ? 100 : percent);
    engine->ui.power = (JelliPowerReading){(uint8_t)power_state(state), level};
}

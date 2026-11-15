#include "auto_sleep_policy.h"

enum { IDLE_TIMEOUT_MS = 60000 };

bool jelli_auto_sleep_allowed(bool reading_valid, uint8_t status, bool button_available,
                              bool network_offline, bool resuming)
{
    return reading_valid && (status & 0x20u) == 0u && button_available && network_offline &&
           !resuming;
}

bool jelli_auto_sleep_should_wake(bool active, bool eligible, bool blank, bool touch,
                                  bool button_pressed)
{
    return active && (!eligible || !blank || touch || button_pressed);
}

bool jelli_auto_sleep_idle(uint64_t now, uint64_t last_activity)
{
    return now >= last_activity && now - last_activity >= IDLE_TIMEOUT_MS;
}

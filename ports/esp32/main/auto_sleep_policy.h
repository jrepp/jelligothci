#ifndef JELLI_AUTO_SLEEP_POLICY_H
#define JELLI_AUTO_SLEEP_POLICY_H
#include <stdbool.h>
#include <stdint.h>

bool jelli_auto_sleep_allowed(bool reading_valid, uint8_t status, bool button_available,
                              bool network_offline, bool resuming);
bool jelli_auto_sleep_should_wake(bool active, bool eligible, bool blank, bool touch,
                                  bool button_pressed);
bool jelli_auto_sleep_idle(uint64_t now, uint64_t last_activity);
#endif

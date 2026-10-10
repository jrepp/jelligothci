#ifndef JELLI_ESP_APP_SWITCH_H
#define JELLI_ESP_APP_SWITCH_H

#include <stdbool.h>

/* Debug hook for "app agent" (ADR-013). Accepts only a loki_agent image in ota_0
 * and arms the switch; returns false, leaving the pet running, otherwise. */
bool jelli_esp_app_switch_arm(void *ctx);

/* The agent is the board's default app (ADR-015). Called once at start-up: selects a
 * loki_agent image in ota_0 for the next reset, on trial like "app agent", unless that
 * agent's last start failed (its trial was aborted, or it was marked invalid); then
 * the pet stays the boot app until a new agent is flashed. The pet keeps running
 * until the next reset either way. */
void jelli_esp_app_switch_default(void);

/* Call each engine iteration after the session checkpoint. Once armed and with no
 * debug reply pending, drains USB output, points the bootloader at ota_0 (which
 * verifies the whole image), and restarts. A rejected image disarms and logs. */
void jelli_esp_app_switch_poll(bool reply_pending);

#endif

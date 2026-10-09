#ifndef JELLI_ESP_APP_SWITCH_H
#define JELLI_ESP_APP_SWITCH_H

#include <stdbool.h>

/* Debug hook for "app agent" (ADR-013). Accepts only a loki_agent image in ota_0
 * and arms the switch; returns false, leaving the pet running, otherwise. */
bool jelli_esp_app_switch_arm(void *ctx);

/* Call each engine iteration after the session checkpoint. Once armed and with no
 * debug reply pending, drains USB output, points the bootloader at ota_0 (which
 * verifies the whole image), and restarts. A rejected image disarms and logs. */
void jelli_esp_app_switch_poll(bool reply_pending);

#endif

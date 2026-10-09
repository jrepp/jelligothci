#include "boot.h"
#include "commands_spike.h"
#include "console_wire.h"
#include "screen.h"
#include "esp_log.h"

static const char *TAG = "loki_agent";

/* RFC-003 phase 0: the agent app's skeleton. It shows an identity screen, answers
 * read-only hardware probes on the @A1 console, and switches back to the pet. */
void app_main(void)
{
    ESP_LOGI(TAG, "Board: Waveshare %s", JELLI_BOARD_LABEL);
    ESP_LOGI(TAG, "Jelli agent %s (RFC-003 phase 0)", JELLI_VERSION_LABEL);
    if (!jelli_agent_console_install()) {
        /* Unconfirmed: the next reset returns to the pet. */
        ESP_LOGE(TAG, "Console unavailable; reset to return to the pet");
        return;
    }
    if (!jelli_agent_screen_start())
        ESP_LOGW(TAG, "Display unavailable; the console still works");
    /* Wi-Fi and audio start before the confirmation, so a crash in them rolls back. */
    jelli_agent_spike_start();
    /* Confirm once start-up has survived and the way back (the console driver) works,
     * but before the console takes commands, so "boot pet" cannot race the
     * confirmation's rewrite of otadata. */
    jelli_agent_boot_confirm();
    if (!jelli_agent_console_run()) {
        ESP_LOGE(TAG, "Console task unavailable");
        return;
    }
    ESP_LOGI(TAG, "Agent ready; \"boot pet\" on the console returns to the pet");
}

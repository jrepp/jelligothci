#include "boot.h"
#include "commands_setup.h"
#include "console_wire.h"
#include "loki_commands.h"
#include "screen.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "loki_agent";

/* Loki Agent (RFC-003): the identity screen, the link to Loki Bridge with hold to
 * talk and spoken replies, and the @A1 console. */
void app_main(void)
{
    ESP_LOGI(TAG, "Board: Waveshare %s", JELLI_BOARD_LABEL);
    ESP_LOGI(TAG, "Loki Agent %s (RFC-003)", JELLI_VERSION_LABEL);
    if (!jelli_agent_console_install()) {
        /* Unconfirmed: the next reset returns to the pet. */
        ESP_LOGE(TAG, "Console unavailable; reset to return to the pet");
        return;
    }
    if (!jelli_agent_screen_start())
        ESP_LOGW(TAG, "Display unavailable; the console still works");
    /* Wi-Fi, audio, and the link start before the confirmation, so a crash in them
     * rolls back. The TLS identity is made after Wi-Fi starts (identity.h). */
    jelli_agent_setup_start();
    jelli_agent_loki_start(jelli_agent_setup_audio_ready(), jelli_agent_setup_wifi_ready());
    /* Confirm once start-up has survived and the way back (the console driver) works,
     * but before the console takes commands, so "boot pet" cannot race the
     * confirmation's rewrite of otadata. */
    jelli_agent_boot_confirm();
    if (!jelli_agent_console_run()) {
        ESP_LOGE(TAG, "Console task unavailable");
        return;
    }
    ESP_LOGI(
        TAG,
        "Agent ready; \"boot pet\" on the console returns to the pet (main stack %u bytes spare)",
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

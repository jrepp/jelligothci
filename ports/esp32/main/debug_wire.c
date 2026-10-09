#include "debug_wire.h"
#include "sound_output.h"
#include "network.h"
#include "jelli/debug.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"

static JelliDebug debug;
static char received[64];
static size_t received_size, received_at;
static uint64_t pending_since;

void jelli_debug_wire_init(void)
{
    /* Startup-only driver allocations: 4 KiB TX, 1 KiB RX plus SDK metadata.
     * The TX ring accepts each complete response atomically, including its LF
     * prefix, so console logs cannot split a protocol line into multiple writes. */
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 4096, .rx_buffer_size = 1024};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
    debug.sound = jelli_sound_output_request;
    debug.command = jelli_network_command;
    ESP_LOGI("debug", "@J1 debug ready; capture timeout 5s idle / 30s total");
}

bool jelli_debug_wire_poll(JelliPetEngine *engine, uint64_t now)
{
    bool was_captured = debug.captured;
    (void)jelli_debug_frozen(&debug, engine, now);
    if (!debug.reply_size) {
        if (received_at == received_size) {
            int size = usb_serial_jtag_read_bytes(received, sizeof(received), 0);
            received_size = size > 0 ? (size_t)size : 0u;
            received_at = 0;
        }
        for (unsigned n = 0; n < sizeof(received) && received_at < received_size; ++n) {
            char byte = received[received_at];
            received[received_at++] = 0;
            jelli_debug_feed(&debug, engine, byte, now);
            if (debug.reply_size) {
                pending_since = now;
                break;
            }
        }
    }
    if (debug.reply_size) {
        int sent = usb_serial_jtag_write_bytes(debug.reply, debug.reply_size, 0);
        if ((sent > 0 && (size_t)sent == debug.reply_size) || now < pending_since ||
            now - pending_since >= 2000u)
            debug.reply_size = 0;
    }
    /* Drop queued touch during capture, including the iteration that releases
     * it. A stale tap must not execute unexpectedly after the CLI disconnects. */
    if (was_captured || debug.captured) {
        JelliInput ignored;
        for (unsigned n = 0; n < 8u && engine->platform.poll; ++n) {
            if (!engine->platform.poll(engine->platform.ctx, &ignored))
                break;
        }
    }
    return jelli_debug_frozen(&debug, engine, now);
}

#include "debug_usb.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

static char received[64];
static size_t received_size, received_at;
static uint64_t pending_since;

void jelli_debug_usb_init(void)
{
    /* Startup-only rings. A whole response fits the TX ring; its leading LF
     * keeps SDK logs outside protocol framing. No allocation while polling. */
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 4096, .rx_buffer_size = 1024};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
}

void jelli_debug_usb_poll(JelliDebugProtocol *protocol, JelliDebugFeed feed, void *ctx,
                          uint64_t now)
{
    if (!protocol->reply_size) {
        if (received_at == received_size) {
            int size = usb_serial_jtag_read_bytes(received, sizeof(received), 0);
            received_size = size > 0 ? (size_t)size : 0u;
            received_at = 0;
        }
        for (unsigned n = 0; n < sizeof(received) && received_at < received_size; ++n) {
            char byte = received[received_at];
            received[received_at++] = 0;
            feed(protocol, ctx, byte, now);
            if (protocol->reply_size) {
                pending_since = now;
                break;
            }
        }
    }
    if (protocol->reply_size) {
        int sent = usb_serial_jtag_write_bytes(protocol->reply, protocol->reply_size, 0);
        if ((sent > 0 && (size_t)sent == protocol->reply_size) || now < pending_since ||
            now - pending_since >= 2000u)
            protocol->reply_size = 0;
    }
}

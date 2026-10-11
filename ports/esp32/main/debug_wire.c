#include "debug_wire.h"
#include "sound_output.h"
#include "network.h"
#include "jelli/debug.h"
#include "debug_usb.h"
#include "esp_log.h"

static JelliDebug debug;
static void feed(JelliDebugProtocol *protocol, void *ctx, char byte, uint64_t now)
{
    (void)protocol;
    jelli_debug_feed(&debug, ctx, byte, now);
}

void jelli_debug_wire_init(JelliDisplayOutput *display)
{
    jelli_debug_usb_init();
    debug.sound = jelli_sound_output_request;
    debug.command = jelli_display_output_command;
    debug.command_ctx = display;
    ESP_LOGI("debug", "@J1 debug ready; capture timeout 5s idle / 30s total");
}

bool jelli_debug_wire_poll(JelliPetEngine *engine, uint64_t now)
{
    bool was_captured = debug.captured;
    (void)jelli_debug_frozen(&debug, engine, now);
    jelli_debug_usb_poll(&debug.protocol, feed, engine, now);
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

#ifndef JELLI_REGISTER_IO_H
#define JELLI_REGISTER_IO_H
#include "jelli/device.h"
#include <stddef.h>

/* One addressed device. Host owns bus handle/address/timeout and serialization.
 * Each transfer is bounded by the adapter; count is bytes (1..16). No retention
 * of caller buffers. read/write may return a native positive error code.
 * Optional wait/clock are used only by blocking setup/cleanup protocols, never
 * by engine frame callbacks. A delay must yield in a task-based host. */
typedef struct {
    void *ctx;
    JelliDeviceResult (*read)(void *ctx, uint8_t reg, uint8_t *data, size_t count);
    JelliDeviceResult (*write)(void *ctx, uint8_t reg, const uint8_t *data, size_t count);
    uint64_t (*now_ms)(void *ctx);
    void (*wait_ms)(void *ctx, unsigned ms);
} JelliRegisterIo;
#endif

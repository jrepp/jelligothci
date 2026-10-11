#ifndef JELLI_DEVICE_H
#define JELLI_DEVICE_H
#include <stdbool.h>
#include <stdint.h>

/* Zero is success. Positive values preserve adapter-native errors; negative
 * values are portable errors. Never interpret an unavailable device as idle. */
typedef int32_t JelliDeviceResult;
enum {
    JELLI_DEVICE_OK = 0,
    JELLI_DEVICE_UNAVAILABLE = -1,
    JELLI_DEVICE_INVALID = -2,
    JELLI_DEVICE_TIMEOUT = -3,
    JELLI_DEVICE_STATE = -4,
    JELLI_DEVICE_RESPONSE = -5,
    JELLI_DEVICE_UNTRUSTED = -6,
    JELLI_DEVICE_IO = -7
};

/* Host owns context and serializes callbacks. Read/write may do bounded IO:
 * call from host session work, never ISR/render callbacks. UTC milliseconds.
 * A successful read asserts source validity. Hosts must additionally apply
 * durable initialization/trust policy before using it for offline progression.
 * Failure leaves output unchanged. NULL callbacks mean unsupported. */
typedef struct {
    void *ctx;
    JelliDeviceResult (*read)(void *ctx, uint64_t *utc_ms);
    JelliDeviceResult (*write)(void *ctx, uint64_t utc_ms);
} JelliClockDriver;

/* Read an already acquired event without blocking. At most one event per frame.
 * The adapter consumes the event exactly once; coalesce bursts before delivery.
 * A motion observation does not assert pickup, orientation, or MCU wake. */
typedef struct {
    uint64_t observed_ms; /* Same monotonic epoch as JelliPlatform.now_ms. */
    bool detected;
} JelliMotionSample;
typedef struct {
    void *ctx;
    JelliDeviceResult (*poll)(void *ctx, JelliMotionSample *sample);
} JelliMotionDriver;

/* Requested percent, not calibrated luminance. Callback finishes the command
 * before returning; failure may leave hardware state unknown. No automatic retry.
 * Sleep is intentionally absent until a production wake policy is qualified. */
typedef struct {
    void *ctx;
    JelliDeviceResult (*brightness)(void *ctx, unsigned percent);
} JelliDisplayDriver;

JelliDeviceResult jelli_clock_read(JelliClockDriver driver, uint64_t *utc_ms);
JelliDeviceResult jelli_clock_write(JelliClockDriver driver, uint64_t utc_ms);
JelliDeviceResult jelli_display_brightness(JelliDisplayDriver driver, unsigned percent);
#endif

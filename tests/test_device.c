#include "jelli/device.h"
#include "jelli/motion.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static JelliDeviceResult failed_read(void *ctx, uint64_t *value)
{
    (void)ctx;
    *value = 123;
    return JELLI_DEVICE_UNTRUSTED;
}

static JelliDeviceResult brightness(void *ctx, unsigned percent)
{
    unsigned *requested = ctx;
    *requested = percent;
    return 42; /* Native transport error must survive the boundary. */
}

int main(void)
{
    uint64_t value = 789;
    CHECK(jelli_clock_read((JelliClockDriver){0}, &value) == JELLI_DEVICE_UNAVAILABLE);
    CHECK(jelli_clock_read((JelliClockDriver){.read = failed_read}, &value) ==
          JELLI_DEVICE_UNTRUSTED);
    CHECK(value == 789);
    CHECK(jelli_clock_write((JelliClockDriver){0}, 0) == JELLI_DEVICE_UNAVAILABLE);
    unsigned requested = 60;
    JelliDisplayDriver display = {&requested, brightness};
    CHECK(jelli_display_brightness(display, 101) == JELLI_DEVICE_INVALID);
    CHECK(requested == 60);
    CHECK(jelli_display_brightness(display, 0) == 42 && requested == 0);
    CHECK(jelli_display_brightness((JelliDisplayDriver){0}, 50) == JELLI_DEVICE_UNAVAILABLE);
    JelliMotionMailbox box = {0};
    JelliMotionSample sample = {0};
    jelli_motion_publish(&box, JELLI_DEVICE_OK, (JelliMotionSample){10, true});
    jelli_motion_publish(&box, JELLI_DEVICE_OK, (JelliMotionSample){20, true});
    jelli_motion_publish(&box, JELLI_DEVICE_OK, (JelliMotionSample){0});
    CHECK(jelli_motion_poll(&box, &sample) == JELLI_DEVICE_OK);
    CHECK(sample.detected && sample.observed_ms == 20);
    CHECK(jelli_motion_poll(&box, &sample) == JELLI_DEVICE_OK && !sample.detected);
    jelli_motion_publish(&box, JELLI_DEVICE_OK, (JelliMotionSample){30, true});
    jelli_motion_publish(&box, JELLI_DEVICE_UNAVAILABLE, (JelliMotionSample){0});
    CHECK(jelli_motion_poll(&box, &sample) == JELLI_DEVICE_UNAVAILABLE);
    jelli_motion_publish(&box, JELLI_DEVICE_OK, (JelliMotionSample){0});
    CHECK(jelli_motion_poll(&box, &sample) == JELLI_DEVICE_OK && !sample.detected);
    return 0;
}

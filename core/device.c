#include "jelli/device.h"

JelliDeviceResult jelli_clock_read(JelliClockDriver driver, uint64_t *utc_ms)
{
    if (!utc_ms)
        return JELLI_DEVICE_INVALID;
    if (!driver.read)
        return JELLI_DEVICE_UNAVAILABLE;
    uint64_t value = 0;
    JelliDeviceResult result = driver.read(driver.ctx, &value);
    if (result == JELLI_DEVICE_OK)
        *utc_ms = value;
    return result;
}

JelliDeviceResult jelli_clock_write(JelliClockDriver driver, uint64_t utc_ms)
{
    return driver.write ? driver.write(driver.ctx, utc_ms) : JELLI_DEVICE_UNAVAILABLE;
}

JelliDeviceResult jelli_display_brightness(JelliDisplayDriver driver, unsigned percent)
{
    if (percent > 100u)
        return JELLI_DEVICE_INVALID;
    return driver.brightness ? driver.brightness(driver.ctx, percent) : JELLI_DEVICE_UNAVAILABLE;
}

#include "rtc_clock.h"
#include "jelli/drivers/pcf85063.h"

/* Match the injected callback signature without incompatible function casts. */
// cppcheck-suppress constParameterCallback
static JelliDeviceResult read_clock(void *ctx, uint64_t *utc_ms)
{
    const JelliRegisterIo *io = ctx;
    return jelli_pcf85063_read(*io, utc_ms);
}

/* Context is borrowed; the transport owns the mutable peripheral. */
// cppcheck-suppress constParameterCallback
static JelliDeviceResult write_clock(void *ctx, uint64_t utc_ms)
{
    const JelliRegisterIo *io = ctx;
    return jelli_pcf85063_write(*io, utc_ms);
}

JelliClockDriver jelli_rtc_clock_open(JelliRegisterIo *io, i2c_master_bus_handle_t bus)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x51, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t device = NULL;
    if (bus && i2c_master_bus_add_device(bus, &config, &device) != ESP_OK)
        device = NULL;
    *io = jelli_esp_register_io(device);
    return (JelliClockDriver){io, read_clock, write_clock};
}

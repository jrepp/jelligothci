#include "jelli/drivers/axp2101.h"

JelliDeviceResult jelli_axp2101_sample(JelliRegisterIo io, JelliAxp2101Sample *sample)
{
    if (!io.read || !sample)
        return JELLI_DEVICE_INVALID;
    JelliAxp2101Sample next = {0};
    JelliDeviceResult result = io.read(io.ctx, 0x00, next.status, sizeof(next.status));
    if (result != JELLI_DEVICE_OK)
        return result;
    result = io.read(io.ctx, 0x30, &next.adc_enable, 1u);
    if (result != JELLI_DEVICE_OK)
        return result;
    next.battery_present = (next.status[0] & 0x08u) != 0u;
    if (next.battery_present && (next.adc_enable & 0x01u)) {
        uint8_t raw[2];
        result = io.read(io.ctx, 0x34, raw, sizeof(raw));
        if (result != JELLI_DEVICE_OK)
            return result;
        next.battery_mv = (uint16_t)(((uint16_t)(raw[0] & 0x3fu) << 8u) | raw[1]);
        next.voltage_valid = true;
    }
    if (next.battery_present) {
        result = io.read(io.ctx, 0xa4, &next.gauge_percent, 1u);
        if (result != JELLI_DEVICE_OK)
            return result;
        next.gauge_valid = next.gauge_percent <= 100u;
    }
    *sample = next;
    return JELLI_DEVICE_OK;
}

#include "jelli/drivers/pcf85063.h"

static unsigned month_days(unsigned year, unsigned month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1u] + (month == 2u && year % 4u == 0u ? 1u : 0u);
}

static bool bcd(uint8_t value, unsigned maximum, unsigned *out)
{
    unsigned low = value & 15u, high = value >> 4u;
    *out = high * 10u + low;
    return low <= 9u && high <= 9u && *out <= maximum;
}

static uint8_t to_bcd(unsigned value) { return (uint8_t)((value / 10u) * 16u + value % 10u); }

JelliDeviceResult jelli_pcf85063_read(JelliRegisterIo io, uint64_t *utc_ms)
{
    if (!utc_ms || !io.read)
        return JELLI_DEVICE_INVALID;
    uint8_t data[11];
    JelliDeviceResult result = io.read(io.ctx, 0, data, sizeof(data));
    if (result != JELLI_DEVICE_OK)
        return result;
    if ((data[0] & 0xa2u) || (data[4] & 0x80u))
        return JELLI_DEVICE_UNTRUSTED;
    unsigned second, minute, hour, day, month, year;
    if (!bcd(data[4] & 0x7fu, 59u, &second) || !bcd(data[5] & 0x7fu, 59u, &minute) ||
        !bcd(data[6] & 0x3fu, 23u, &hour) || !bcd(data[7] & 0x3fu, 31u, &day) ||
        !bcd(data[9] & 0x1fu, 12u, &month) || !bcd(data[10], 99u, &year) || !day || !month)
        return JELLI_DEVICE_INVALID;
    year += 2000u;
    if (day > month_days(year, month))
        return JELLI_DEVICE_INVALID;
    uint64_t days = 10957u; /* Days from 1970-01-01 to 2000-01-01. */
    for (unsigned y = 2000u; y < year; ++y)
        days += y % 4u == 0u ? 366u : 365u;
    for (unsigned m = 1u; m < month; ++m)
        days += month_days(year, m);
    *utc_ms =
        ((days + day - 1u) * 86400u + (uint64_t)hour * 3600u + (uint64_t)minute * 60u + second) *
        1000u;
    return JELLI_DEVICE_OK;
}

JelliDeviceResult jelli_pcf85063_write(JelliRegisterIo io, uint64_t utc_ms)
{
    uint64_t seconds = utc_ms / 1000u;
    if (!io.read || !io.write || seconds < UINT64_C(946684800) || seconds >= UINT64_C(4102444800))
        return JELLI_DEVICE_INVALID;
    uint64_t days = seconds / 86400u - 10957u;
    unsigned year = 2000u, month = 1u;
    while (days >= (year % 4u == 0u ? 366u : 365u)) {
        days -= year % 4u == 0u ? 366u : 365u;
        ++year;
    }
    while (days >= month_days(year, month)) {
        days -= month_days(year, month);
        ++month;
    }
    uint8_t control;
    JelliDeviceResult result = io.read(io.ctx, 0, &control, 1);
    if (result != JELLI_DEVICE_OK)
        return result;
    uint8_t stop[] = {(uint8_t)((control & 1u) | 0x20u)};
    uint8_t start[] = {(uint8_t)(control & 1u)};
    result = io.write(io.ctx, 0, stop, sizeof(stop));
    /* Even an ambiguous STOP write needs a best-effort restart. */
    if (result == JELLI_DEVICE_OK) {
        uint8_t date[] = {to_bcd((unsigned)(seconds % 60u)),
                          to_bcd((unsigned)(seconds / 60u % 60u)),
                          to_bcd((unsigned)(seconds / 3600u % 24u)),
                          to_bcd((unsigned)days + 1u),
                          (uint8_t)((seconds / 86400u + 4u) % 7u),
                          to_bcd(month),
                          to_bcd(year - 2000u)};
        result = io.write(io.ctx, 4, date, sizeof(date));
    }
    JelliDeviceResult restarted = io.write(io.ctx, 0, start, sizeof(start));
    return result == JELLI_DEVICE_OK ? restarted : result;
}

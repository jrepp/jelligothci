#include "jelli/drivers/qmi8658.h"
#include <string.h>

/* QST QMI8658C Rev A, document 13-52-27: sections 5.2, 5.10 and 12.
 * Register reads are single-byte because reset CTRL1 disables auto-increment.
 * No reset, calibration, FIFO, gyro or power-rail commands are issued. */
static const uint8_t registers[JELLI_QMI_REG_COUNT] = {2, 3, 4, 6, 8, 9, 10, 11, 12, 20};
static const uint8_t baseline[JELLI_QMI_REG_COUNT] = {0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static JelliDeviceResult read_byte(JelliRegisterIo io, uint8_t reg, uint8_t *value)
{
    return io.read(io.ctx, reg, value, 1);
}

static JelliDeviceResult write_byte(JelliRegisterIo io, uint8_t reg, uint8_t value)
{
    return io.write(io.ctx, reg, &value, 1);
}

static JelliDeviceResult snapshot(JelliRegisterIo io, uint8_t *values)
{
    for (unsigned i = 0; i < JELLI_QMI_REG_COUNT; ++i) {
        JelliDeviceResult error = read_byte(io, registers[i], &values[i]);
        if (error != JELLI_DEVICE_OK)
            return error;
    }
    return JELLI_DEVICE_OK;
}

static JelliDeviceResult wait_done(JelliRegisterIo io, bool done, uint8_t *status)
{
    uint64_t started = io.now_ms(io.ctx);
    for (unsigned i = 0; i < 40; ++i) {
        JelliDeviceResult error = read_byte(io, 0x2d, status);
        if (error != JELLI_DEVICE_OK)
            return error;
        if (((*status & 0x80u) != 0) == done)
            return JELLI_DEVICE_OK;
        uint64_t now = io.now_ms(io.ctx);
        if (now < started || now - started >= 200u)
            break;
        io.wait_ms(io.ctx, 5);
    }
    return JELLI_DEVICE_TIMEOUT;
}

static JelliDeviceResult command(JelliRegisterIo io, JelliQmi8658 *probe, uint8_t value)
{
    uint8_t status;
    probe->command_error = JELLI_DEVICE_OK;
    probe->ack_error = JELLI_DEVICE_OK;
    JelliDeviceResult error = read_byte(io, 0x2d, &status);
    if (error != JELLI_DEVICE_OK || (status & 0x80u)) {
        probe->command_error = error != JELLI_DEVICE_OK ? error : JELLI_DEVICE_STATE;
        return probe->command_error;
    }
    error = write_byte(io, 10, value);
    if (error == JELLI_DEVICE_OK)
        error = wait_done(io, true, &status);
    /* ACK is cleanup, not a retry of the command, including ambiguous writes. */
    JelliDeviceResult ack = write_byte(io, 10, 0);
    if (ack == JELLI_DEVICE_OK)
        ack = wait_done(io, false, &probe->ack_status);
    probe->command_error = error;
    probe->ack_error = ack;
    if (error == JELLI_DEVICE_OK && ack == JELLI_DEVICE_OK)
        ++probe->commands;
    return error == JELLI_DEVICE_OK ? ack : error;
}

static JelliDeviceResult identify(JelliRegisterIo io, JelliQmi8658 *probe)
{
    JelliDeviceResult error = command(io, probe, 0x10);
    for (unsigned i = 0; error == JELLI_DEVICE_OK && i < 3; ++i)
        error = read_byte(io, (uint8_t)(0x49u + i), &probe->firmware[i]);
    for (unsigned i = 0; error == JELLI_DEVICE_OK && i < 6; ++i)
        error = read_byte(io, (uint8_t)(0x51u + i), &probe->usid[i]);
    return error;
}

static JelliDeviceResult arm(JelliRegisterIo io, JelliQmi8658 *probe, bool enable_int2)
{
    /* 21 Hz, +/-2g, 128mg slope, INT2 initial low, 8 blanked samples.
     * Disable DRDY on INT2 so data-ready cannot masquerade as motion. */
    JelliDeviceResult error = write_byte(io, 3, 0x0d);
    if (error == JELLI_DEVICE_OK)
        error = write_byte(io, 11, 128);
    if (error == JELLI_DEVICE_OK)
        error = write_byte(io, 12, 0x48);
    if (error == JELLI_DEVICE_OK) {
        probe->wom_attempted = true;
        error = command(io, probe, 8);
    }
    if (error == JELLI_DEVICE_OK)
        error = write_byte(io, 8, 0x21);
    /* Explicit A-compatible comparator: exact-board SensorLib enables INT2
     * here; QMI8658A Rev A documents bit4, C Rev A calls it reserved. */
    if (error == JELLI_DEVICE_OK && enable_int2)
        error = write_byte(io, 2, 0x30);
    if (error == JELLI_DEVICE_OK)
        error = snapshot(io, probe->armed);
    uint8_t expected[JELLI_QMI_REG_COUNT] = {0x20, 0x0d, 0, 0, 0x21, 0x80, 0, 128, 0x48, 0};
    expected[0] = enable_int2 ? 0x30 : 0x20;
    if (error == JELLI_DEVICE_OK && memcmp(probe->armed, expected, sizeof(expected)) != 0)
        error = JELLI_DEVICE_RESPONSE;
    return error;
}

static JelliDeviceResult check_baseline(JelliRegisterIo io, JelliQmi8658 *probe)
{
    JelliDeviceResult error = read_byte(io, 0, &probe->who);
    if (error == JELLI_DEVICE_OK)
        error = read_byte(io, 1, &probe->revision);
    if (error == JELLI_DEVICE_OK && (probe->who != 5 || probe->revision != 0x7c))
        error = JELLI_DEVICE_UNAVAILABLE;
    if (error == JELLI_DEVICE_OK)
        error = snapshot(io, probe->before);
    if (error != JELLI_DEVICE_OK)
        return error;
    /* Reject unknown owners/configurations; hidden prior WoM cannot be backed up. */
    if (memcmp(probe->before, baseline, sizeof(baseline)) != 0)
        return JELLI_DEVICE_STATE;
    uint8_t status;
    error = read_byte(io, 0x2d, &status);
    if (error != JELLI_DEVICE_OK || (status & 0x80u))
        return error != JELLI_DEVICE_OK ? error : JELLI_DEVICE_STATE;
    return JELLI_DEVICE_OK;
}

JelliDeviceResult jelli_qmi8658_begin(JelliRegisterIo io, JelliQmi8658 *probe, bool motion,
                                      bool enable_int2)
{
    if (!probe)
        return JELLI_DEVICE_INVALID;
    *probe = (JelliQmi8658){0};
    if (!io.read || !io.write || !io.now_ms || !io.wait_ms || (enable_int2 && !motion))
        return JELLI_DEVICE_INVALID;
    JelliDeviceResult error = check_baseline(io, probe);
    if (error != JELLI_DEVICE_OK)
        return error;
    probe->saved = true;
    probe->changed = true;
    error = write_byte(io, 9, 0x80); /* Polling handshake, without INT1 assertion. */
    if (error == JELLI_DEVICE_OK)
        error = identify(io, probe);
    static const uint8_t qualified_firmware[] = {0x1e, 3, 1};
    if (error == JELLI_DEVICE_OK && enable_int2 &&
        memcmp(probe->firmware, qualified_firmware, sizeof(qualified_firmware)) != 0)
        error = JELLI_DEVICE_UNAVAILABLE;
    if (error == JELLI_DEVICE_OK && motion)
        error = arm(io, probe, enable_int2);
    return error;
}

JelliDeviceResult jelli_qmi8658_ack_motion(JelliRegisterIo io, JelliQmi8658 *probe)
{
    if (!probe || !io.read)
        return JELLI_DEVICE_INVALID;
    /* Caller records GPIO level before this clear-on-read operation. */
    JelliDeviceResult error = read_byte(io, 0x2f, &probe->status);
    probe->status_valid = error == JELLI_DEVICE_OK;
    return error;
}

JelliDeviceResult jelli_qmi8658_restore(JelliRegisterIo io, JelliQmi8658 *probe)
{
    if (!probe || !io.read || !io.write || !io.wait_ms || !io.now_ms)
        return JELLI_DEVICE_INVALID;
    if (!probe->changed)
        return JELLI_DEVICE_OK;
    JelliDeviceResult result = write_byte(io, 8, 0);
    if (probe->wom_attempted) {
        io.wait_ms(io.ctx, 100); /* At least 2/21 Hz before reconfiguration. */
        JelliDeviceResult error = write_byte(io, 11, 0);
        if (error == JELLI_DEVICE_OK)
            error = command(io, probe, 8);
        if (error != JELLI_DEVICE_OK)
            result = error;
    }
    /* Attempt each saved writable setting even if another cleanup failed.
     * CTRL9 is ACK-only, not an arbitrary replay of a saved command. */
    for (unsigned i = 0; i < JELLI_QMI_REG_COUNT; ++i) {
        if (registers[i] == 4 || registers[i] == 6 || registers[i] == 20)
            continue; /* Observed but never modified. */
        JelliDeviceResult error = write_byte(io, registers[i], probe->before[i]);
        if (error != JELLI_DEVICE_OK)
            result = error;
    }
    JelliDeviceResult acknowledged = wait_done(io, false, &probe->ack_status);
    if (result == JELLI_DEVICE_OK)
        result = acknowledged;
    JelliDeviceResult readback = snapshot(io, probe->after);
    probe->restored = result == JELLI_DEVICE_OK && readback == JELLI_DEVICE_OK &&
                      memcmp(probe->before, probe->after, sizeof(probe->before)) == 0;
    if (probe->restored)
        probe->changed = false;
    return probe->restored ? JELLI_DEVICE_OK : JELLI_DEVICE_RESPONSE;
}

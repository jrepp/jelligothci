#include "jelli/drivers/pcf85063.h"
#include "jelli/drivers/qmi8658.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    uint8_t regs[128];
    uint64_t now;
    unsigned calls, fail_at, waits, writes, acks;
    bool stuck_done, stuck_ack, frozen_time, wom, qmi;
} Fake;

static JelliDeviceResult read_regs(void *ctx, uint8_t reg, uint8_t *data, size_t count)
{
    Fake *f = ctx;
    CHECK(count && count <= 16 && count <= sizeof(f->regs) - reg);
    CHECK(!f->qmi || count == 1);
    if (++f->calls == f->fail_at)
        return 99;
    memcpy(data, f->regs + reg, count);
    if (reg == 0x2f)
        f->regs[reg] = 0; /* clear-on-read */
    return JELLI_DEVICE_OK;
}

static JelliDeviceResult write_regs(void *ctx, uint8_t reg, const uint8_t *data, size_t count)
{
    Fake *f = ctx;
    CHECK(count && count <= 16 && count <= sizeof(f->regs) - reg);
    memcpy(f->regs + reg, data, count); /* Failed writes may still reach hardware. */
    ++f->writes;
    if (f->qmi && reg == 10) {
        CHECK(count == 1 && (*data == 0 || *data == 8 || *data == 0x10));
        if (*data == 8)
            f->wom = f->regs[11] != 0;
        if (*data == 0) {
            ++f->acks;
            if (!f->stuck_ack)
                f->regs[0x2d] = 0;
        } else if (!f->stuck_done)
            f->regs[0x2d] = 0x80;
    }
    return ++f->calls == f->fail_at ? 99 : JELLI_DEVICE_OK;
}

static uint64_t now_ms(void *ctx) { return ((Fake *)ctx)->now; }
static void wait_ms(void *ctx, unsigned ms)
{
    Fake *f = ctx;
    ++f->waits;
    if (!f->frozen_time)
        f->now += ms;
}
static JelliRegisterIo transport(Fake *f)
{
    return (JelliRegisterIo){f, read_regs, write_regs, now_ms, wait_ms};
}
static void reset_qmi(Fake *f)
{
    *f = (Fake){.qmi = true};
    f->regs[0] = 5;
    f->regs[1] = 0x7c;
    f->regs[2] = 0x20;
    f->regs[0x49] = 0x1e;
    f->regs[0x4a] = 3;
    f->regs[0x4b] = 1;
}
static void qmi_success(void)
{
    Fake f;
    reset_qmi(&f);
    JelliQmi8658 sensor;
    JelliRegisterIo io = transport(&f);
    CHECK(jelli_qmi8658_begin(io, &sensor, true, true) == JELLI_DEVICE_OK);
    CHECK(f.wom && f.regs[2] == 0x30 && f.regs[8] == 0x21 && sensor.commands == 2);
    f.regs[0x2f] = 4;
    CHECK(jelli_qmi8658_ack_motion(io, &sensor) == JELLI_DEVICE_OK);
    CHECK(sensor.status_valid && sensor.status == 4 && f.regs[0x2f] == 0);
    CHECK(jelli_qmi8658_restore(io, &sensor) == JELLI_DEVICE_OK);
    CHECK(sensor.restored && !f.wom && !sensor.changed && f.regs[2] == 0x20);
    unsigned calls = f.calls;
    CHECK(jelli_qmi8658_restore(io, &sensor) == JELLI_DEVICE_OK && calls == f.calls);
}
static void qmi_failures(void)
{
    Fake f;
    JelliQmi8658 sensor;
    reset_qmi(&f);
    f.regs[1] = 0;
    CHECK(jelli_qmi8658_begin(transport(&f), &sensor, true, true) == JELLI_DEVICE_UNAVAILABLE);
    CHECK(!f.writes);
    reset_qmi(&f);
    f.regs[2] = 0x30;
    CHECK(jelli_qmi8658_begin(transport(&f), &sensor, false, false) == JELLI_DEVICE_STATE);
    CHECK(!f.writes);
    reset_qmi(&f);
    f.regs[0x49] = 0;
    CHECK(jelli_qmi8658_begin(transport(&f), &sensor, true, true) == JELLI_DEVICE_UNAVAILABLE);
    CHECK(!sensor.wom_attempted);
    CHECK(jelli_qmi8658_restore(transport(&f), &sensor) == JELLI_DEVICE_OK);
    reset_qmi(&f);
    f.stuck_done = true;
    f.frozen_time = true;
    CHECK(jelli_qmi8658_begin(transport(&f), &sensor, false, false) == JELLI_DEVICE_TIMEOUT);
    CHECK(f.waits == 40 && f.acks == 1 && sensor.ack_error == JELLI_DEVICE_OK);
    CHECK(jelli_qmi8658_restore(transport(&f), &sensor) == JELLI_DEVICE_OK);
    reset_qmi(&f);
    f.stuck_ack = true;
    CHECK(jelli_qmi8658_begin(transport(&f), &sensor, false, false) == JELLI_DEVICE_TIMEOUT);
    CHECK(sensor.command_error == JELLI_DEVICE_OK && sensor.ack_error == JELLI_DEVICE_TIMEOUT);
    CHECK(jelli_qmi8658_restore(transport(&f), &sensor) != JELLI_DEVICE_OK);
    CHECK(!sensor.restored && sensor.changed);
}
static void qmi_fault_sweep(void)
{
    Fake f;
    JelliQmi8658 sensor;
    reset_qmi(&f);
    CHECK(jelli_qmi8658_begin(transport(&f), &sensor, true, true) == JELLI_DEVICE_OK);
    unsigned operations = f.calls;
    for (unsigned fail = 1; fail <= operations; ++fail) {
        reset_qmi(&f);
        f.fail_at = fail;
        CHECK(jelli_qmi8658_begin(transport(&f), &sensor, true, true) != JELLI_DEVICE_OK);
        f.fail_at = 0;
        CHECK(jelli_qmi8658_restore(transport(&f), &sensor) == JELLI_DEVICE_OK);
        CHECK(!f.wom && f.regs[2] == 0x20 && f.regs[8] == 0);
    }
    reset_qmi(&f);
    CHECK(jelli_qmi8658_begin(transport(&f), &sensor, true, true) == JELLI_DEVICE_OK);
    f.stuck_done = true; /* Readback match cannot substitute for successful WoM exit. */
    CHECK(jelli_qmi8658_restore(transport(&f), &sensor) != JELLI_DEVICE_OK);
    CHECK(!sensor.restored && sensor.changed);
}
static void qmi_cleanup_faults(void)
{
    Fake f;
    JelliQmi8658 sensor;
    reset_qmi(&f);
    CHECK(jelli_qmi8658_begin(transport(&f), &sensor, true, true) == JELLI_DEVICE_OK);
    f.calls = 0;
    CHECK(jelli_qmi8658_restore(transport(&f), &sensor) == JELLI_DEVICE_OK);
    unsigned operations = f.calls;
    for (unsigned fail = 1; fail <= operations; ++fail) {
        reset_qmi(&f);
        CHECK(jelli_qmi8658_begin(transport(&f), &sensor, true, true) == JELLI_DEVICE_OK);
        f.calls = 0;
        f.fail_at = fail;
        CHECK(jelli_qmi8658_restore(transport(&f), &sensor) != JELLI_DEVICE_OK);
        CHECK(!sensor.restored && sensor.changed);
    }
}

static void rtc_calendar(void)
{
    Fake f = {0};
    JelliRegisterIo io = transport(&f);
    static const uint64_t seconds[] = {946684800, 951782400, 1709164800, 4102444799};
    for (unsigned i = 0; i < sizeof(seconds) / sizeof(seconds[0]); ++i) {
        CHECK(jelli_pcf85063_write(io, seconds[i] * 1000u) == JELLI_DEVICE_OK);
        uint64_t actual = 0;
        CHECK(jelli_pcf85063_read(io, &actual) == JELLI_DEVICE_OK);
        CHECK(actual == seconds[i] * 1000u);
    }
    CHECK(jelli_pcf85063_write(io, UINT64_MAX) == JELLI_DEVICE_INVALID);
    CHECK(jelli_pcf85063_write(io, 0) == JELLI_DEVICE_INVALID);
    Fake stopped = f;
    stopped.regs[4] = 0x80;
    uint64_t value = 123;
    CHECK(jelli_pcf85063_read(transport(&stopped), &value) == JELLI_DEVICE_UNTRUSTED &&
          value == 123);
    f.regs[4] = 0x6a;
    CHECK(jelli_pcf85063_read(io, &value) == JELLI_DEVICE_INVALID && value == 123);
    CHECK(jelli_pcf85063_write(io, UINT64_C(1709164800000)) == JELLI_DEVICE_OK);
    CHECK(f.regs[7] == 0x29 && f.regs[9] == 2 && f.regs[10] == 0x24);
    f.regs[10] = 0x23;
    CHECK(jelli_pcf85063_read(io, &value) == JELLI_DEVICE_INVALID);
}
static void rtc_partial_writes(void)
{
    for (unsigned fail = 2; fail <= 4; ++fail) {
        Fake f = {.fail_at = fail};
        CHECK(jelli_pcf85063_write(transport(&f), UINT64_C(946684800000)) == 99);
        CHECK(!(f.regs[0] & 0x20u)); /* Restart attempted after ambiguous STOP/date. */
        CHECK(f.writes >= 2);
    }
}
int main(void)
{
    qmi_success();
    qmi_failures();
    qmi_fault_sweep();
    qmi_cleanup_faults();
    rtc_calendar();
    rtc_partial_writes();
    puts("Injected register IO, cleanup, bounded waits and RTC trust verified.");
    return 0;
}

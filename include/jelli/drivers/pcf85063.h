#ifndef JELLI_PCF85063_H
#define JELLI_PCF85063_H
#include "jelli/drivers/register_io.h"

/* 2000..2099 UTC. Reads reject OS, STOP, 12h and test mode. A successful read
 * still needs the host's durable initialization/trust gate. Outputs unchanged
 * on error. Writes preserve only the existing capacitor selection, as before;
 * the host must durably revoke trust BEFORE calling write and restore trust
 * only after success. Partial writes attempt restart and report failure. */
JelliDeviceResult jelli_pcf85063_read(JelliRegisterIo io, uint64_t *utc_ms);
JelliDeviceResult jelli_pcf85063_write(JelliRegisterIo io, uint64_t utc_ms);
#endif

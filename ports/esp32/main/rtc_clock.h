#ifndef JELLI_RTC_CLOCK_H
#define JELLI_RTC_CLOCK_H
#include "register_io.h"
/* Startup only. Caller owns io and must keep it alive for the returned driver.
 * Absence is represented by an unavailable read, never by a fabricated epoch. */
JelliClockDriver jelli_rtc_clock_open(JelliRegisterIo *io, i2c_master_bus_handle_t bus);
#endif

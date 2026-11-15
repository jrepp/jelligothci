#ifndef JELLI_AXP2101_H
#define JELLI_AXP2101_H
#include "jelli/drivers/register_io.h"

/* Raw status is retained for diagnostics. Voltage is meaningful only when
 * battery_present and voltage_valid are true. The gauge is an uncalibrated
 * PMIC estimate, not a measurement of charge; gauge_valid only checks range.
 * Reads do not change ADC, charge, rail, interrupt or gauge settings. */
typedef struct {
    uint16_t battery_mv;
    uint8_t status[2], adc_enable, gauge_percent;
    bool battery_present, voltage_valid, gauge_valid;
} JelliAxp2101Sample;

/* Bounded register IO belongs in a host worker, never a frame or ISR callback.
 * On any read failure the output is unchanged. */
JelliDeviceResult jelli_axp2101_sample(JelliRegisterIo io, JelliAxp2101Sample *sample);
#endif

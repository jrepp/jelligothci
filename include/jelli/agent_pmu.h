#ifndef JELLI_AGENT_PMU_H
#define JELLI_AGENT_PMU_H

#include <stdbool.h>
#include <stdint.h>

/* Read-only decode of the AXP2101 registers that RFC-003 phase 0 needs to know:
 * whether the microphone supply (ALDO1) is on, and what the PWR key does. Field
 * layout from X-Powers' AXP2101 datasheet as implemented by XPowersLib:
 *   0x22 bit 0: long press restarts (1) or powers off (0); bit 1: long-press
 *        shutdown enabled; bit 2: level-2 over-temperature shutdown enabled.
 *   0x27 bits 1:0 power-on press 128 ms/512 ms/1 s/2 s; bits 3:2 power-off press
 *        4/6/8/10 s; bits 5:4 IRQ press 1/1.5/2/2.5 s.
 *   0x90 bit 0: ALDO1 enabled.  0x92 bits 4:0: ALDO1 = 500 mV + 100 mV per step. */
enum {
    JELLI_AGENT_PMU_POWER_OFF_ENABLES = 0x22,
    JELLI_AGENT_PMU_KEY_LEVELS = 0x27,
    JELLI_AGENT_PMU_LDO_ENABLES = 0x90,
    JELLI_AGENT_PMU_ALDO1_VOLTAGE = 0x92
};

typedef struct {
    bool aldo1_on, long_press_off, long_press_restarts, over_temperature_off;
    unsigned aldo1_mv, on_ms, off_ms, irq_ms;
} JelliAgentPmu;

/* Decodes the four raw register values, in the order of the enum above. */
JelliAgentPmu jelli_agent_pmu_decode(uint8_t power_off_enables, uint8_t key_levels,
                                     uint8_t ldo_enables, uint8_t aldo1_voltage);

#endif

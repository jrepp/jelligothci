#include "jelli/agent_pmu.h"

JelliAgentPmu jelli_agent_pmu_decode(uint8_t power_off_enables, uint8_t key_levels,
                                     uint8_t ldo_enables, uint8_t aldo1_voltage)
{
    static const unsigned on_ms[4] = {128u, 512u, 1000u, 2000u};
    static const unsigned off_ms[4] = {4000u, 6000u, 8000u, 10000u};
    static const unsigned irq_ms[4] = {1000u, 1500u, 2000u, 2500u};
    JelliAgentPmu pmu;
    pmu.long_press_restarts = (power_off_enables & 1u) != 0u;
    pmu.long_press_off = (power_off_enables & 2u) != 0u;
    pmu.over_temperature_off = (power_off_enables & 4u) != 0u;
    pmu.on_ms = on_ms[key_levels & 3u];
    pmu.off_ms = off_ms[(key_levels >> 2) & 3u];
    pmu.irq_ms = irq_ms[(key_levels >> 4) & 3u];
    pmu.aldo1_on = (ldo_enables & 1u) != 0u;
    pmu.aldo1_mv = 500u + 100u * (aldo1_voltage & 0x1fu);
    return pmu;
}

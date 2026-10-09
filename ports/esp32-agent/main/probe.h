#ifndef JELLI_AGENT_PROBE_H
#define JELLI_AGENT_PROBE_H

#include <stddef.h>
#include <stdint.h>

/* Read-only hardware probes for RFC-003 phase 0. Each writes one JSON object
 * ({"ok":true,...} or {"ok":false,"error":...}) into out. None writes a PMU
 * register, flash, or NVS; pwr and te block the console for their duration. */
void jelli_agent_probe_chip(char *out, size_t capacity);
void jelli_agent_probe_heap(char *out, size_t capacity);
void jelli_agent_probe_pmu(char *out, size_t capacity);
/* Samples the PWR key level every 2 ms for 2..10000 ms. */
void jelli_agent_probe_pwr(char *out, size_t capacity, uint32_t ms);
/* Counts panel tearing-effect pulses with the pulse counter for 100..5000 ms. */
void jelli_agent_probe_te(char *out, size_t capacity, uint32_t ms);

#endif

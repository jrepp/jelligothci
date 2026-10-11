---
title: Factory inventory baseline and ranked validation passes
author: Engineering Team
created: 2026-10-10T20:15:57Z
tags: [memo, technical]
id: memo-048
project_id: jelligotchi
doc_uuid: 74453808-291d-43f5-ab50-509a897c93b2
---

# Overview

Started the user's impact-ranked validation work with a reusable factory inventory
pass and a [methodology for every opportunity](../../docs/device-validation-passes.md).
Touch scheduling/recovery and display sleep/brightness lead expected impact;
inventory is their prerequisite, not a claim that the lower-impact sensor probes
are the largest optimization. Later A/B treatments remain to be implemented and
measured, with physical/current prerequisites explicitly identified.

# Implementation

Factory schema 3 adds `pmic.inventory`, `imu.identity` and `rtc.snapshot` through
the existing shared debug parser, USB transport, retained suite results and host
capture. A board-owned allowlist captures 25 PMU registers, nine IMU registers,
and a coherent RTC calendar/control burst plus timer control. No register data
is written; no IRQ status is acknowledged, sensor enabled or time set. QMI status
and FIFO reads are excluded. The factory image still excludes the game engine,
assets, saves and networking.

Each result identifies address, register/value samples and valid prefix length.
Failed reads cannot masquerade as zeros. IMU pass additionally requires identity
0x05. RTC pass means successful access, not a trusted calendar. The station rejects
incomplete passing samples and wrong identities; derived observations preserve
unknown UTC trust and unvalidated gauge accuracy. Historical schema 2 is rejected
by the new station instead of silently accepting an unexpected catalog.

Three fixed inventories consume 201 bytes of sample/configuration storage plus
three handles/error slots and startup-only SDK handle metadata. No new task,
framebuffer or per-run allocation is added. Each transaction has a 50 ms timeout;
the largest case stops on first error and has a 1.25 s transaction timeout budget.
All results still fit the existing report buffer. Tests remain single-main-task
owned. One bounded case executes between command polls.

# Hardware evidence

Discovered `/dev/cu.usbmodem2101`, serial identity `90:70:69:FE:21:DC`, then flashed
the factory image with verified transfer hashes. Ten independent full captures
on the same boot each yielded six passes, zero errors and four fixture skips.
The station exited 2 for incomplete coverage as intended. No manufacturing
acceptance was granted. This is a repeatability baseline, not a reset/retention
or physical functionality qualification.

| Observation | Evidence / implication |
| --- | --- |
| PMU inventory | 25/25 registers read, stable across ten runs; median 12,248 us, nearest-rank p95 12,250 us. ADC enable 0x03, gauge raw 100; neither voltage nor SOC accuracy established. |
| IMU | Address 0x6b, WHO_AM_I 0x05, revision 0x7c, CTRL1 0x20, CTRL7 0x00. Accelerometer and gyro disabled; oscillator-disable bit clear. Median/p95 4,429 us. |
| RTC | Address 0x51, Control1/2 0x00, seconds first 0x84 and last 0xa1; calendar advances but OS remains set. Median 1,882 us, p95 1,891 us. Time cannot be trusted for resume. |
| RTC idle configuration | Timer control 0x18 already selects 1/60 Hz; that proposed setting offers no new change. CLKOUT-disable encoding is absent from Control2, making unused clock output a later candidate. |
| Isolated scope | Run 11 executed only IMU identity; one pass, zero errors. RTC result was not_run with valid=0 and cleared samples; old evidence did not leak into a new run. |

The RTC finding is a real prerequisite for long-sleep recovery: do not clear OS
or set a plausible date merely to pass a test. Establish trusted time through the
existing application policy and then separately test retention against station
time. All chip configuration remained unchanged in this pass.

Normal firmware was rebuilt, flashed back with hash verification, and queried
for game capabilities and state. Serial recovery succeeded; physical display,
touch, audio, battery current and RTC power-loss retention were not verified.

# Verification and evidence location

Factory, normal and power firmware builds passed. Profile isolation checks passed
for all three. C formatting/static analysis/size checks passed. Nine station tests
passed, including partial inventory rejection, wrong identity, stale/malformed
results, lost-run safeguards and explicit RTC uncertainty. No portable core logic
changed in this increment. Documentation check and repair validation passed without repairs.

Raw serial, replies, station metadata, derived observations, binary/config hashes,
source patch and new-source copies are under ignored `build/factory-pass-001/`.
Factory ELF SHA256 is
`a2941fab80814297e09d4eb26127f860f68226cecfb0834fdb26eceacf5997b8`.
The remaining work is the ranked treatment/fixture sequence, not a claim that
all opportunities have now been validated.

# References

- [Factory workflow and schema](../../docs/factory-validation.md)
- [Research basis](memo-047-device-capabilities-and-experiment-backlog.md)
- [Shared transport](memo-046-shared-factory-debug-path.md)

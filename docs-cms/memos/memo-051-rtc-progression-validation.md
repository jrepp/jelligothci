---
title: RTC progression validation
author: Engineering Team
created: 2026-10-10T20:44:04Z
tags: [memo, technical]
id: memo-051
project_id: jelligotchi
doc_uuid: 8213170e-294d-42a3-87b1-fa6e7707c857
---

# Overview

The RTC advanced by two seconds in each of ten station-timed intervals on
SKU 31261, device 907069fe21dc. All eleven factory runs completed with six passes,
four fixture skips and zero errors. The oscillator-stop flag stayed set; clock
progression does not establish trusted UTC or safe offline pet progression.

# Method

`tools/debug/rtc_experiment.py` reuses the factory suite, schema validator and
shared debug client. It adds no firmware commands or engine code. Each complete
capture brackets its RTC snapshot with station monotonic timestamps. Success
requires calendar progression within those bounds plus one second of register
quantization. This is a liveness check, not a ppm measurement. The test validates
BCD/calendar dates, configuration stability, run ordering and device/boot/image
identity. It leaves time, configuration and the OS flag untouched.

Ten intervals reported two RTC seconds each. Station lower bounds ranged from
2.0004 to 2.0051 seconds; upper bounds ranged from 2.1034 to 2.1123 seconds. Controls
remained zero, timer configuration remained 0x18, and the calendar remained in
January 2000. IMU inventory still showed revision 0x7c with both sensors disabled.
No current, motion wake, battery retention or long-sleep recovery was tested.

The NXP datasheet explains that OS remains set until cleared by software; a
running oscillator can coexist with OS=1. The production session reader rejects
OS and also requires persisted RTC initialization metadata. Neither condition is
bypassed by this experiment. Explicit clock synchronization remains a prerequisite
for trusted long-sleep recovery. The next independent hardware pass is the QMI8658
CTRL9 handshake and GPIO21 motion interrupt, before attempting motion-triggered
sleep.

# Verification and evidence

Factory firmware rebuilt and passed profile isolation. Host/SDL CTest passed
67/67 tests, including the new station test. Station tests cover invalid
BCD/calendar, stopped/12-hour clocks, stale runs, changed identity, frozen/fast
progression, minute rollover and keeping trust separate from progression.
An initial test incorrectly rejected a one-second increment allowed by the stated
quantization bounds; the test was corrected without tightening the hardware gate.

Ignored `build/rtc-pass-001/` retains flash logs, raw serial/replies, summary,
firmware hashes and copies, runner sources and the tracked source diff. Normal
firmware was restored with transfer hashes verified. The shared debug path reported
profile `game`, `clock_known:false`, healthy buffers/guards/heap, zero transfer
failures and 1420 bytes of main-task stack reserve. This confirms the application
continues to withhold clock trust. Physical display recovery was not observed in
this pass. Documentation checks and repair passed for 70 documents without repairs.

# References

- [RTC station workflow](../../docs/device-validation-passes.md#rtc-progression-prerequisite)
- [Inventory baseline](memo-048-factory-inventory-validation-pass.md)
- [NXP PCF85063A datasheet, OS flag section](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf)

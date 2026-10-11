---
title: IMU command and interrupt qualification
author: Engineering Team
created: 2026-10-10T20:58:45Z
tags: [memo, technical]
id: memo-052
project_id: jelligotchi
doc_uuid: 723185cc-7ed8-4d8c-a96c-ee3038b4ba3a
---

# Overview

The QMI8658 command handshake passed ten identity trials. A further identity
trial captured weak-pull behavior. Firmware bytes were 1e0301 and USID was
32b65a0e65c3 on board 907069fe21dc. WHO=05/revision=7c matched the newer QST
register protocol. The older Rev0.6 command handshake is not suitable for this
qualification.

# Findings and experiments

The first WoM setup matched its register targets and completed all handshakes,
but GPIO21 stayed high with STATUS1=0. Immediate inspection at about 53 ms and
a second build with 600 ms settling both failed the idle-low gate. Each trial
completed explicit WoM exit and register restoration; neither attempted sleep.
The runner latched the failure, and the next revised image was flashed only after
cleanup had been verified.

Research found a second contract difference. C Rev A marks CTRL1.bit4 reserved;
the exact-board vendor bundles an A datasheet defining it as INT2 enable and
its SensorLib WoM function sets it. Both documents can report revision7c, so
identity bytes alone do not resolve the fitted suffix. An explicit comparator
qualifies that behavior for this observed firmware, without changing production.

With original CTRL1=20, GPIO21 followed weak pull-down/up (0/1). With CTRL1=30,
ten quiet trials held it low under both pulls (0/0), reported no WoM events,
and restored every saved control. This supports the interrupt-output enable
hypothesis for this specimen; it does not prove package suffix, continuity on
other boards or manufacturing acceptance. No undocumented generic driver init,
sensor reset or gyro calibration was copied into the probe.

The comparator used 21 Hz accelerometer-only mode, +/-2g, threshold128 mg,
blanking8 samples, idle-low INT2, and DRDY disabled. One-second settling preceded
two-second quiet observations; median total setup/observation was 3,067,312.5 us.
A subsequent 20-second pickup window returned no GPIO/WoM event, with clean
cleanup. Physical stimulus was not yet confirmed, so pickup sensitivity and
wake readiness remain inconclusive. The MCU did not sleep.

# Implementation and validation

Board-specific code lives in `validation/boards/qmi_probe.c`; trial sequencing
lives in `validation/power/imu_trial.c`. The existing shared USB protocol and
station client carry all commands/results. Fixed storage replaces per-trial
allocation, register reads avoid auto-increment assumptions, and every command
uses bounded done/ACK polling. The active probe is excluded from normal/factory
images. Factory read-only inventory remains available unchanged.

Trials preserve raw readings and separate operation and cleanup errors. WoM exit
must succeed in addition to a visible-register match. Trial exclusion now rejects
any new run while a panel, touch, power or IMU case is pending/active. An initial
size-check failure in the command dispatcher was fixed by extracting the common
busy check and removing redundant per-mode checks; limits were not raised.

All three firmware profiles built and passed isolation. Host/SDL CTest passed
68/68 tests. C analysis and size
checks passed over 207 files. Station tests reject stale identity, missing motion
status, missing GPIO evidence, wrong comparator configuration and failed cleanup.
Hardware evidence is retained in ignored `build/imu-pass-001/`: raw serial,
requests/replies, successful and unsuccessful cases, flashed image/configuration
hashes and source snapshots. No current or long-sleep game-resume claim is made.

Normal firmware was restored with verified flash hashes; the debug path reported
profile `game`, clock unknown, healthy display buffers/guards/heap and zero
transfer failures. Physical display confirmation was not repeated. Documentation
check and repair passed for 71 documents without repairs; serial clients closed.

# References

- [Reusable IMU workflow](../../docs/device-validation-passes.md#imu-command-and-interrupt-qualification)
- [RTC prerequisite](memo-051-rtc-progression-validation.md)
- [QST QMI8658C Rev A](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf)
- [Exact-board vendor SensorLib](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/e4344e70c2fa78a13e8a06566507f1ba8af6672a/examples/arduino/libraries/SensorLib/src/SensorQMI8658.hpp)
- [Exact-board bundled QMI8658A datasheet](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/e4344e70c2fa78a13e8a06566507f1ba8af6672a/examples/arduino/libraries/SensorLib/datasheet/QMI8658A%20Datasheet%20Rev%20A.pdf)

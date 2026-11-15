---
title: Battery monitor and power history validation
author: Engineering Team
created: 2026-11-15T16:27:57Z
tags: [memo, technical]
id: memo-116
project_id: jelligotchi
doc_uuid: 87f2a9b4-e278-4894-8eec-2cc62bd93816
---

# Observation

The normal game samples the board's AXP2101 on a worker and exposes the latest
reading in Settings and through `battery`. A bounded NVS history records boot,
sampled source changes, display blanking, pet sleep and periodic observations;
`powerlog` retrieves records through the existing debug protocol. See the
[hardware guide](../../docs/hardware.md#battery-and-power-history) for command
usage, storage limits, coalescing and interpretation.

# Verification and limits

The ESP-IDF 6.1 game from a separate local worktree built and flashed to the
connected board with verified transfer hashes. USB debug reported a present
battery at approximately 3.59 V and a PMIC estimate of 13%. A deliberate reset
left an earlier record readable and added a new boot record. Serial startup
reported 8 MiB PSRAM with a passing memory test, panel creation, touch input
registration and the game-ready message. The battery worker reported a minimum
2040-byte stack margin in this short observation; the display task reported an
840-byte margin. The observed USB reset was initiated
by the monitor. Those observations do not establish physical display/touch,
voltage accuracy, gauge calibration, current, real hardware sleep or long-term
flash retention. The local worktree was based on older gameplay changes and used
ESP-IDF 6.1; the current main branch uses a different SDK pin and needs its own
build and CI evidence. Logs and a protected pre-test NVS backup are in ignored
`build/power-*` files.

The desktop suite and sanitizer suite each passed 113 tests on the separate local
worktree; the portable driver, history codec and settings-render tests also passed
when run against the current-main based worktree. These do not establish physical
behavior. A prior desktop run exposed a factory schema fixture mismatch; updating
the RTC fixture restored the targeted test, tracked in the factory telemetry PR.

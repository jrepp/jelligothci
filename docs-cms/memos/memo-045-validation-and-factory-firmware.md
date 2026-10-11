---
title: Reusable power experiments and standalone factory smoke firmware
author: Engineering Team
created: 2026-10-10T19:29:37Z
tags: [memo, technical]
id: memo-045
project_id: jelligotchi
doc_uuid: 66affa70-1229-4ef8-8f2c-81d4cd1d4c88
---

The initial `@JF1` stream described below is historical. Current firmware uses
the shared `@J1` debug path; see [memo-046](memo-046-shared-factory-debug-path.md).

# Overview

Power research now lives in optional validation code. Normal firmware retains
TX-only audio and the disabled-network blocking wait, but excludes probe setup,
IRQ/wake counters, trial scratch state, PM profiling and experiment commands.
The core engine has no experiment dependency. Small host hooks inline away.

A separate factory profile compiles only its entry point and shared PMIC probe
from application sources. It excludes core, assets, sessions and networking.
Factory runs do not initialize NVS or load saves. The non-destructive smoke suite
establishes a serial/test pattern; it does not grant production acceptance.

# Layout and maintenance

`validation/boards/` owns board-specific transactions. The bounded PMIC read
probe is shared by the power and factory profiles. `validation/power/` owns
quiesce/restore trials. `validation/factory/` owns the standalone entry point.
The factory host parser requires a complete, consistent transcript and records
station identity, capture UUID, device MAC, image ELF hash and raw results.

`make esp-build`, `scripts/esp validate` and `scripts/esp factory` use separate
build directories and generated configurations. An old project-directory
`sdkconfig` is preserved but no longer selected by the wrapper. Intentional
local settings must be transferred explicitly; profile checks catch drift.
CI builds all three with the pinned SDK and checks profile source/config
isolation. Firmware CI and desktop tests exercise malformed factory transcripts.
Source formatting, Cppcheck and size checks include all validation code.

# Hardware observations

The refactored power profile was flashed and captured in `trials-profile/`.
Pause and explicit light sleep returned operation/restoration error zero.
Light sleep held 103,214 microseconds, restored in 1,384,407 microseconds and
woke on timer cause 4. LVGL ticks and network wakes remained zero. Display
buffers, guards and heap checks passed, with zero transfer failures. This
capture preceded extracting the unchanged PMIC read loop into the shared probe;
that probe was then exercised by the factory image.

The standalone factory image booted to `@JF1 ready` without starting the pet.
Two captures completed with run IDs 1 and 2, three passes and four explicit
skips. Host exit code 2 correctly classified both as incomplete coverage,
with acceptance false. First-run PSRAM scratch took 144 microseconds; the four
PMIC reads took 2,171 microseconds and returned 74/8/15/255, matching the earlier
power image. MAC was 907069fe21dc. Factory ELF SHA256 was:

```text
1691e9bc10cdf89d34c38e818e865f52d13f3f09e1f43348df60c64ee6ba948c
```

The SDK emits an I2C pull-up advisory at startup. Transactions succeeded; this
is not proof of electrical margins. No rail, touch-mode or provisioning writes
were added. Physical display, touch, audio and current remain skipped.

# Validation and evidence

Evidence is retained under ignored `build/device-power-research/`: profile build
and flash logs, `startup-validation-profile.log`, `startup-factory.log`,
`trials-profile/`, and `factory-run-01/` / `factory-run-02/`. Image binaries,
configurations and hashes are retained alongside the captures. Flash logs verify
transfer hashes. A monitor-triggered reset is distinct from a crash.

An initial power build overlapped a source-file move and failed with a missing
old path. Reconfiguration after the move completed successfully. This was a
build sequencing issue, not a driver failure; finish source relocation before
starting compilation. Later builds used the final paths.

Normal firmware was restored and booted with debug readiness. It rejected the
`power` command; the power recorder stopped during preflight before submitting
a trial. Post-restore display checks reported zero mismatches and transfer
failures, intact guards/heap, and 1,468 bytes sampled main-task stack reserve.
See `startup-normal-restored.log`, `normal-restored-display.json`, and
`normal-preflight-rejection/` (the raw serial log retains the rejected reply).
No physical display or touch confirmation is inferred from these checks.

All three final profiles built, and source/config isolation checks passed.
`make test` passed 63/63, including factory transcript rejection cases.
`make lint-c` passed formatting, clang-tidy, Cppcheck and all size gates.
Shell syntax/ShellCheck, workflow action/OS pins and actionlint passed.
Documentation validation passed and repair made no changes. Hosted CI is wired
up but has not run remotely in this worktree. `organized-images.json` records
final binary hashes and sizes; `image-organized-*.bin`, corresponding generated
configurations and `organized-source.patch` retain the reviewable artifacts.

# Remaining work

Production acceptance still needs approved board/revision identity, fixture
capabilities, stimulus and thresholds for each physical/electrical test. New
board profiles must be registered in CI and supported explicitly by the host
parser. Related product names are not evidence of pin/protocol compatibility.
Provisioning, calibration, eFuse programming and recovery/erasure workflows are
outside this smoke suite. The design ADR remains proposed for review.

# References

- [Validation layout](../../ports/esp32/main/validation/README.md)
- [Factory contract and extension pattern](../../docs/factory-validation.md)
- [Proposed architecture](../adr/adr-013-board-validation-profiles.md)
- [Original bench findings](memo-044-device-power-experiments.md)

---
title: Display sleep and brightness validation
author: Engineering Team
created: 2026-10-10T20:37:58Z
tags: [memo, technical]
id: memo-050
project_id: jelligotchi
doc_uuid: 0a509b07-e81c-4522-91c6-9ce2a4288a92
---

# Overview

The display pass completed 80 bounded transitions on SKU 31261 with no reported
command/restoration errors. Ordinary sleep reduced median command-level recovery
from 1,364,578 us for deep standby to 121,489 us, approximately 11.2 times faster.
The user observed the sequence and reported: "that all worked and we're back to
the original visual state". Current was not measured; ordinary sleep remains a
validation-only option, with normal firmware using the original driver policy.

# Method and implementation

The power image exercises the real board driver/LVGL pipeline over shared `@J1`
USB transport. `power panel run MODE` queues a trial with boot/run correlation;
`power panel result` reads retained evidence. The station rejects mismatched,
failed or unrestored results and never retries an ambiguous action. Each case
pauses LVGL, drains preceding SPI color transfers through a checked blocking
brightness command, enters a treatment, holds 200 ms, restores panel/brightness,
resumes LVGL and requests a full repaint. Diagnostic time is excluded from live
pet advancement and queued input discarded. It creates no task or framebuffer;
fixed result storage is 768 bytes plus small engine-owned state. A failed case
latches the panel runner against further trials until reboot.

Treatments are baseline, requested brightness 30 percent, 10 percent, black,
display off, ordinary sleep and deep standby, followed by the baseline again.
The original brightness was the standard 60 percent startup setting throughout.
Display health is checked after its five-second reporting interval. The final
logical framebuffer is retained separately from physical observer evidence.

## Ordinary sleep policy seam

The pinned CO5300 2.2.0 driver always enters deep standby when hardware reset is
configured. A validation-only CMake helper verifies the SPI source SHA256
`823bda75000ebe4236ef06911cf26fc873e6507c8cc5de17a5104d89d19a9c34`, generates a
build-local copy and adds one explicit policy condition to DSTBON entry. The
existing driver still owns all sleep/deep-standby/reset/init flags, and its own
SLPIN/SLPOUT path retains the existing 120 ms waits. The policy defaults to deep
standby and is restored after each trial. Reset wiring and both 600 ms module
initialization waits remain unchanged. No private layout cast or raw-command
bypass was introduced; managed dependency files remain intact.

Only power builds use the generated source. Normal/factory compile the original,
and profile isolation verifies that distinction. A deliberately changed dummy
source was rejected by the hash guard. The driver became an explicit direct
manifest dependency at the same resolved 2.2.0 version; there was no upgrade.

## Brightness evidence limitation

BSP 3.0.1 discards the error from its brightness SPI write and returns ESP_OK.
Its getter converts its cached byte back to integer percent; repeated getter-to-
setter restoration can lose precision. The panel trial instead calls the public
CO5300 brightness API, which returns transmission errors, and restores the fixed
baseline without updating the BSP cache for each treatment. Values in reports
are requested software state, not hardware register readback or calibrated
luminance. This pass does not change production brightness defaults or fix every
older caller of the BSP API.

# Hardware results

The current USB path was rediscovered as `/dev/cu.usbmodem2101`, serial identity
`90:70:69:FE:21:DC`. An eight-case smoke run preceded the final 80-case matrix.
Every command and restoration reported success, and requested restoration remained
60 percent. Timing below ends at command completion, before full repaint/visible
frame completion. The baseline combines initial and restored groups.

| Mode | Runs | Recovery median (us) | Nearest-rank p95 (us) |
| --- | --- | --- | --- |
| baseline | 20 | 1490 | 1551 |
| dim30 | 10 | 1423 | 1451 |
| dim10 | 10 | 1444.5 | 1464 |
| black | 10 | 1489.5 | 1514 |
| off | 10 | 1536.5 | 1569 |
| sleep | 10 | 121489 | 121529 |
| standby | 10 | 1.36458e+06 | 1364616 |

Post-matrix display checks: buffers equal, guards/heap intact, zero mismatches,
zero transfer failures, and 912 bytes reported main-task stack reserve. The
initial smoke observed 816 bytes; this is the instrumented power profile, not
a new guaranteed stack bound. No new allocations occur per trial.

The user's visual observation supports image restoration for the observed pass.
It does not establish photometric/color calibration, exact first-visible-frame
latency, current savings or battery life. Device JSON intentionally retains
`physical_verified:false` and `current_measured:false`: those fields describe
what the automated probe establishes. The separate observer record carries the
human evidence without rewriting raw replies. No production acceptance is granted.

# Verification and remaining work

All three firmware builds and profile isolation checks passed. Host/SDL CTest
passed 66/66 tests, including rejection of stale boot/run, failed restoration,
brightness mismatch and unsupported physical/current claims. C analysis and
size checks passed over 203 files. An initial build needed the driver header as
an explicit component requirement; adding the existing pinned dependency resolved
it. The trial's first execute function exceeded complexity 20; separating entry
transition handling resolved it without changing thresholds. The source hash
rejection was also exercised with a dummy unreviewed driver.

Normal firmware was restored with verified transfer hashes and queried for game
capabilities, state and display diagnostics. Buffers/guards/heap remained healthy
with no reported transfer failures. Documentation check/repair passed without
repairs, and serial clients were closed. The managed driver hash remained unchanged.
The next gate for promoting ordinary sleep is external current measurement versus
deep standby and repeated physical wake under touch/game load. No automatic sleep
policy or PMIC rail changes were made. The RTC trust prerequisite from memo-048
still applies before using long hardware sleep for game progression.

# Evidence and references

Ignored `build/panel-pass-001/` retains raw serial, replies, logical frames, summary
statistics, observer feedback, hash-rejection evidence, images/configuration,
source snapshot, build logs and flash/restoration logs.

- [Display pass workflow](../../docs/device-validation-passes.md#display-transitions-and-brightness-pass)
- [Prior touch pass](memo-049-touch-recovery-and-scheduling-validation.md)
- [Capability research](memo-047-device-capabilities-and-experiment-backlog.md)
- [Factory inventory and RTC finding](memo-048-factory-inventory-validation-pass.md)

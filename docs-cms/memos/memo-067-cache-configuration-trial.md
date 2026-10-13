---
title: Isolated cache configuration trial
author: Codex
created: 2026-10-11
tags: [embedded, performance, tooling, validation]
id: memo-067
project_id: jelligotchi
doc_uuid: 98f5b40a-f4e4-4232-8f20-a0f1438efb00
---

# Scope

The user requested building a cache trial after web research and the
[LVGL dispatch trial](memo-071-lvgl-dispatch-and-empty-overlay-trial.md).
The initial increment prepared isolated power-profile images and configuration checks.
It does not promote cache settings to production or establish a performance gain.
The subsequent authorized device attempt and recovery are recorded below.

The hypothesis is that reducing instruction/data cache misses can reduce the
remaining LVGL work. Espressif recommends larger caches and cache lines in its
[LVGL adapter guidance](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html).
The experiment uses the existing pinned SDK and renderer, without adopting the
guide's other recommendations for threads, XIP, clocks or refresh cadence.

# Trial and resource budget

The [cache defaults](../../ports/esp32/profiles/cache/baseline.defaults) and
[matrix checker](../../tools/debug/check_cache_trial.py) define the baseline,
three independent changes and a combined candidate. Instruction-cache capacity,
data-cache capacity and data-cache line length are tested separately before
interpreting their combination. All profiles use the original draw buffers.
Overlay traversal and built-in copy remain baseline during captures.

For this trial the larger instruction cache reserves an additional 16384 bytes
and the larger data cache an additional 32768 bytes; combined, 49152 bytes are
unavailable to the heap relative to baseline. Line length alone adds no cache
capacity. These are cache reservations, not measured reductions in a particular
heap capability. The pinned SDK's
`components/esp_system/port/soc/esp32s3/Kconfig.cache` describes the heap tradeoff.
No new framebuffers, tasks, queues, stack allocations or DMA ownership changes
are introduced. Existing rendering and profiling allocations still apply.

The [memory-placement trial](memo-065-back-buffer-memory-placement-trial.md)
recorded constrained internal RAM, and memo-071 observed a startup bounce-buffer
allocation failure on one recovery boot. A successful link cannot establish
that every cache candidate has enough internal/DMA heap to start and run.
Record free internal/DMA memory, largest allocatable block and stack headroom
before accepting any timing. A candidate that cannot initialize is a rejected
resource configuration, not a reason to shrink buffers or change other settings
inside this comparison.

# Build and verification

Run from the repository root:

```sh
./scripts/esp validate
for mode in baseline icache dcache line64 icache-line64 combined; do
  ./scripts/esp cache-trial "$mode" || exit 1
done
python3 tools/debug/check_cache_trial.py
python3 tests/test_cache_trial.py
```

The [wrapper](../../scripts/esp) gives each mode its own generated configuration
and build directory under `build/esp32-cache-MODE`. Normal, power and factory
output directories remain separate. These commands compile without flashing.
SDK defaults do not override an existing generated configuration: the checker
rejects stale or manually altered settings rather than silently accepting them.

The checker requires the baseline to match the current power configuration,
rejects non-cache differences throughout the matrix, checks generated settings
and power-profile isolation, and records application, ELF, bootloader and config
SHA-256 identities in `build/cache-trial-manifest.json`. It removes an older
manifest before rechecking so a failed check cannot leave an earlier acceptance
report in place. The manifest describes build artifacts, not device results.
Rebuild every image and rerun the checker after source changes; hashes alone
do not establish that an old binary was built from the current source tree.

# Device protocol

Discover the board's current port and retain the normal recovery image before
deployment. Cache settings are startup configuration, so every treatment needs
a separate boot. Use the wrapper's matching mode when explicitly flashing an
image, and retain flash hashes and serial startup output with the build manifest.
Verify firmware identity, PSRAM test, panel/touch readiness and runtime display
health before starting a capture. Stop on panic, allocation or health failures
and restore normal firmware; do not automatically cycle through failing images.

Use the existing [pipeline station](../../tools/debug/pipeline_experiment.py)
with identical scene, copy mode, overlays, memory mode, capture duration and
forced-refresh cadence. For example, after selecting and verifying a boot:

```sh
./scripts/uv run --python 3.12 tools/debug/pipeline_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/cache-capture-baseline-01 \
  --scene "normal pet animation; record actual scene and state" \
  --seconds 10 --refresh-ms 500 --memory-mode 0 \
  --memcpy builtin --overlays baseline
```

Use at least three captures per treatment across independent boots, interleaving
baseline/candidate and candidate/baseline order. Use the same warm-up interval
after readiness and record it. Retain game state and scene evidence because
elapsed time and reboots can change the pet. Report medians of per-capture LVGL
p95, work/flush/wait attribution and heap/stack margins; do not add stage
percentiles or compare against older captures as if they were matched controls.
The existing fine event probes remain enabled in every image, so any gain is
instrumented until qualified with reduced instrumentation.

Final acceptance still requires output equivalence and physical display/touch
verification. Energy or battery-life claims require current measurements.
Restore and health-check normal firmware after device trials, and record that
deployment separately from compilation evidence.

# Validation

All five cache images compiled successfully from the working tree based on
`ab2b6f73364b3b44d36f91ca13b2e12a0f450e0b`, including the pre-existing uncommitted
hardware trials. The baseline matched the active power configuration. The
matrix checker accepted only the intended cache differences and all five
power-profile isolation checks. Normal, power and factory builds and their
isolation checks also passed after the wrapper change. Application, ELF,
bootloader and configuration hashes are retained in the manifest; build logs
are under ignored `build/cache-trial-build-001`.

An initial configuration comparison rejected the larger instruction cache
because Kconfig omits its now-unavailable disabled 16-byte line choice. The
checker now treats an absent symbol as equivalent to a disabled symbol, while
still rejecting newly enabled symbols. All seven cache-checker tests passed,
including that regression. The existing pipeline and factory capture suites
also passed (seven and nine tests respectively). Shell syntax and whitespace
checks passed. No firmware C code changed in this increment.
Documentation validation and repair passed; the sole repair sorted this memo's
tags and was reviewed. No unresolved documentation findings remain.

At completion of the initial build increment, no image had been flashed.

# Device continuation: baseline rejected and normal firmware restored

On the user's instruction to continue, serial discovery found SKU 31261's
Espressif USB interface at `/dev/cu.usbmodem2101`, serial
`90:70:69:FE:21:DC`. The running image identified itself as the game profile;
display equality, guards and heap checks passed before deployment. The matrix
checker passed again and normal recovery artifacts were retained.

Two separate baseline deployments verified flash hashes and booted the expected
baseline ELF identity (`42db28369` prefix). Both logged the same sequence near
2310 ms after startup: SPI `setup_dma_priv_buffer` could not allocate a private
TX buffer, followed by failed panel color transmission and the LVGL bridge
reporting `ESP_ERR_NO_MEM`. Neither boot was accepted for timing. Normal firmware
was restored and health-checked between attempts. The retry followed source
inspection of the startup path; the repeated failure ended the sequence.
No larger-cache or longer-line candidate was flashed, and no performance
capture or cache comparison was accepted.

Source inspection shows an ordering window: the pinned BSP's
`bsp_display_start_with_config` starts the LVGL adapter task before returning,
while [app_main](../../ports/esp32/main/main.c) attaches the application's
[DMA staging callback](../../ports/esp32/main/display_transfer.c) afterward.
An early refresh can therefore use the adapter's default transfer path and
request another bounce allocation despite the application's already reserved
staging buffer. This is consistent with the errors and the earlier observation
in memo-071; it is not yet a device-verified diagnosis of the complete allocation
history. No firmware source or cache settings were changed to mask the failure.

The prerequisite is to qualify display startup ordering so the first refresh
uses the intended bounded staging path, then repeat the baseline across clean
boots before resuming the cache matrix. Do not weaken the startup acceptance
gate or attribute this unchanged-baseline failure to a cache candidate.

An ad hoc PTY monitor helper also failed to terminate cleanly within its timeout;
a process-group signal attempt was denied. Process inspection subsequently
showed no leftover monitor. Final recovery used the regular interactive
`scripts/esp monitor` path, exited successfully with Ctrl+], followed by serial
health requests. For subsequent captures, use that verified monitor lifecycle
until automated startup capture and teardown have been qualified independently.

Final normal deployment verified flash hashes. Startup reported version 0.8.0,
the expected normal ELF identity (`753e356c5` prefix), a monitor-induced
`USB_UART_CHIP_RESET`, a successful PSRAM test, panel creation, touch registration
and application readiness. The final boot had no SPI allocation failure or
panic. The audio driver logged that its channel was not yet enabled during
disable, followed by successful codec and sound initialization; audio output
was not physically tested. Ten subsequent display-health requests passed with
zero mismatches/transfer failures, intact equality/guards/heap and 1356 bytes of
reported stack headroom. Serial was closed after the checks.

Evidence is under ignored `build/cache-device-001`: baseline flash/startup logs,
the configuration/artifact manifest, recovery images and logs, and
`final-health.json`. These are failed baseline acceptance attempts and successful
serial recovery evidence, not cache-performance results. Physical display/touch
confirmation and current measurements remain unavailable. The earlier build
and host-test evidence still applies; this continuation changes documentation
only. Documentation validation and repair were rerun after recording recovery.

The subsequent root-cause fix and resumed optimization measurements are in
[memo-068](memo-068-display-startup-order-and-cache-results.md).

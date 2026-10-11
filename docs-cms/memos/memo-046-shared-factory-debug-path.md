---
title: Shared debug transport for game and factory firmware
author: Engineering Team
created: 2026-10-10T20:01:48Z
tags: [memo, technical]
id: memo-046
project_id: jelligotchi
doc_uuid: 03dc8449-7c23-4bac-896c-5ce593d753a1
---

# Overview

The user asked to close the gap between the game debug path and factory station
validation. All profiles now use the same `@J1` parser, request IDs, response
framing, USB transport and Python client. The factory-specific `@JF1` parser and
stream reader were removed. Factory schema 2 is a queryable command surface;
normal game command semantics and protocol version 1 remain unchanged.

# Boundaries

`core/debug_protocol.c` and its engine-independent header own bounded request
parsing and replies. `JelliDebug` embeds this state and retains game dispatch,
capture timing and input handling. The factory build includes only that portable
protocol source from `core/`, with no game engine, assets or session code.
`debug_usb.c` owns one set of USB buffers and polling rules for both applications.
The station tools share `Client`, serial setup and `RecordedWire`.

Every profile answers `capabilities`. Factory exposes `factory tests`, `status`,
`run [all|TEST_ID]`, and `result RUN_ID TEST_ID`. Running queues a bounded suite;
the main task executes at most one probe between command polls. Another run
while active returns busy. Results remain readable until the next explicit run
or reset. Unselected tests report not_run, old run IDs are rejected, and queries
never trigger physical checks. Game-state commands are explicitly unsupported.

Factory status includes boot nonce, run ID, scope, counters, MAC and ELF hash.
The host checks identity/run continuity and brackets result collection with
status queries. It never retries an ambiguous action. All fixture-dependent
checks remain skipped, and acceptance remains false. This does not add network
access, provisioning, erasure or a production acceptance policy.

# Verification

The shared parser has a standalone C test with no engine dependency, covering
request ID overflow, malformed/oversized lines, pending reply protection and
buffer clearing. Existing game debug/CLI tests cover the preserved state and
framebuffer contract. Station tests exercise wrong profiles, missing/duplicate
results, stale runs, reboot detection, hardware error and no implicit run retry.

The unified factory image was flashed and reached `@J1 debug ready`. The regular
`jelli-debug capabilities` command identified the factory profile and schema 2.
The capture tool ran the full suite and queried seven retained results: three
passes, four skips, acceptance false, with expected exit code 2. A separate
PMIC-only run returned one pass; its identity test remained not_run. The same
client observed rejection of game `state`, an unknown test and a stale run ID.
Those rejected commands left the retained status unchanged.

The SDK I2C pull-up advisory remains; successful reads do not prove electrical
margins. Physical display, touch, acoustic output and battery-current acceptance
remain outside these serial checks.

Evidence is stored under ignored `build/debug-unification/`: build/flash/startup
logs, factory capabilities, `factory-run/`, and `factory-queries.jsonl` with raw
serial capture. Profile isolation checks allow only the shared protocol source
from core in factory builds. CI continues to compile all three profiles.

The unified power image also completed timer light sleep with USB identity
reconnection and a full framebuffer capture through the shared client. Hold was
103,163 microseconds, recovery 1,384,419 microseconds, operation and restoration
errors zero. LVGL ticks and network wakes stayed zero. Guards/heap/framebuffer
checks passed and transfer failures stayed zero. Sampled main-task stack reserve
reached 680 bytes during diagnostic profiling; this remains a margin to monitor
before expanding diagnostic work. No physical touch wake was established.

Final host validation: `make test` 64/64, `make core-test` 28/28, and
`make sanitize` 64/64 passed. C formatting, clang-tidy, Cppcheck and size gates
passed. All three firmware profiles compiled and passed isolation checks.
Documentation validation passed; the repair pass made no changes. These are
local results; hosted CI has not run for this uncommitted worktree.

Normal firmware was restored after validation. Its capabilities identify the
game profile and omit factory/power commands. State and display queries succeed;
factory status is rejected. Display checks report zero mismatches or transfer
failures and intact guards/heap. Sampled main-task stack reserve was 1,380 bytes.
These serial checks do not establish physical display/touch quality. Captures
are `normal-capabilities.json`, `normal-state.json`, `normal-display.json`, and
`normal-factory-rejected.log` alongside the startup/flash logs.

`images.json` records the final binary sizes and hashes; `image-*.bin`, generated
`sdkconfig-*`, and `source.patch` retain the tested configurations and source.
Historical earlier image evidence remains under `build/device-power-research/`.

# Maintenance observations

Cppcheck initially treated reply_size as unchanged across an opaque callback.
Passing the protocol state explicitly to that callback made ownership visible
and removed the false positive without a suppression. Composition lengthened a
state formatter beyond the existing size limit; extracting page-name lookup
restored the limit without changing its JSON contract.

See the current [factory contract](../../docs/factory-validation.md) for commands,
resource budgets, station limits and extension rules. The original factory
stream and its captures in [memo-045](memo-045-validation-and-factory-firmware.md)
are historical, not another supported protocol to maintain.

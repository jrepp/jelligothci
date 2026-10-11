---
title: Board validation profiles and reusable probes
status: Proposed
created: 2026-10-10T19:23:00Z
deciders: Engineering Team  # team or person who made the decision
tags: [architecture, design]
id: adr-013
project_id: jelligotchi
doc_uuid: c723d9e1-c09d-48a2-8635-c86a6eabab4f
---

# Context

Board power research needs maintained code without loading normal game paths
with trial state or peripheral probes. Related boards may follow. A future
manufacturing image will need automated tests with stronger acceptance rules
than exploratory measurements. The user requested this separation; this specific
architecture remains proposed pending review.

# Decision

Keep a small host-only experiment interface with no-op hooks in normal builds.
Compile board probes and trial sequencing only in explicit validation profiles.
Use separate SDK configuration and output directories, but the same pinned SDK
and board dependencies. Build both normal and validation profiles in CI and
check that disabled profiles do not compile experiment sources.

Keep proven driver improvements in normal board support. Keep chip addresses,
reset protocols and board assumptions in named board probe files. Do not create
a universal board API until another supported device establishes shared needs.
Power experiments may borrow the running pet host to reproduce its resource
conditions. They must preserve the engine timing and display ownership contracts.

Provide a factory entry point that calls reusable board probes without
starting a pet session or loading customer saves. Give each board/revision a
profile and capability manifest, stable test IDs, explicit pass/fail/skip/error
results, timeouts and cleanup. Record board identity, firmware hash, protocol
schema and fixture identity in every manufacturing run. Unsupported capabilities
must be skipped explicitly and required skips must prevent acceptance.

The power profile identifies its board, profile and schema. A separate factory
smoke profile shares the portable `@J1` parser, USB transport and Python client
with normal firmware; only its command handlers and state differ. It reports device identity, ELF hash, PSRAM scratch and PMIC read
results. It explicitly skips fixture-only checks and never grants manufacturing
acceptance. No provisioning, calibration, partition erasure, eFuse writes or
manufacturing thresholds are implemented by this change.

# Consequences

Normal builds exclude diagnostic scratch state, probe callbacks and PM profiling.
CI catches SDK/API drift in the opt-in code, but hardware runs remain necessary.
Separate configurations prevent experiment PM defaults from affecting normal
builds. Existing local generated configuration must be migrated explicitly.
The extra firmware CI builds cost time and SDK downloads.

The factory harness reuses probes through a separate boot path and shared
serial protocol. Factory schema 2 adds discovery, status, individual/full runs
and retained result queries; game commands remain unavailable. Production still needs a fixture contract and revision checks. USB-connected serial tests cannot establish unplugged current,
visible panel quality, acoustic output or physical touch coverage.

# Alternatives Considered

Always compiling dormant trials adds storage and cognitive load to normal paths.
A standalone copy of the firmware would drift from the actual driver stack.
An immediate generic board framework or complete production acceptance suite would invent
requirements before another board or production fixture is defined.

# References

- [Experiment layout](../../ports/esp32/main/validation/README.md)
- [Bench results](../memos/memo-044-device-power-experiments.md)
- [Hardware commands](../../docs/hardware.md#power-experiments-over-usb)

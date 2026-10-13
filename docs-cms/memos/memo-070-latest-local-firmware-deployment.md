---
title: Latest local normal firmware deployment
author: Codex
created: 2026-10-12
tags: [deployment, esp32, validation]
id: memo-070
project_id: jelligotchi
doc_uuid: 4b070354-ecbd-4dfb-9dae-0d8ee53de7c7
---

# Scope and deployment

The user requested flashing the latest version. The current local working tree,
based on `ab2b6f73364b3b44d36f91ca13b2e12a0f450e0b` with existing uncommitted
firmware work, was incrementally built with `make esp-build`. The normal profile
isolation check passed. The image descriptor reports version 0.8.0 and ELF SHA256
`f3c5537efe078a269f4082cc3cd47bd5af30acebd7c29b05a2b7ffb7d62bfbbf`, matching the
normal image recorded in [memo-069](memo-069-display-command-stack-margin.md).
No firmware source changes were needed.

Fresh port discovery identified the expected Espressif board at
`/dev/cu.usbmodem2101`, USB serial `90:70:69:FE:21:DC`. The normal flash command
completed successfully and verified all transfer hashes. No full-chip erase
was performed. Build, image descriptor, flash and serial logs are retained under
ignored `build/deploy-20261012/`.

# Verification and remaining uncertainty

The serial monitor's deliberate reset reported `USB_UART_CHIP_RESET`. Startup
reported the expected version and ELF identity, successful PSRAM memory test,
CO5300 panel creation, CST9217 touch registration, deferred LVGL worker startup,
and pet/debug readiness. The session reported an available save and unknown RTC.
The first post-reset display health check reported matching buffers, intact
guards and heap, zero mismatches, and 1396 bytes of main-task stack headroom.
No panic or reboot loop was observed. The monitor was closed after observation.

Panel initialization, I2C pull-up and gesture notices remain. Audio initialization
logged an I2S channel-disable-before-enable error, then successfully opened the
codec and reported sound ready. No corrective change was made; audible behavior
has not been verified in this deployment.

This task verified the firmware build, flash and serial startup. Host tests were
not rerun for this deployment-only task. Physical display colors, animation and
touch response still require operator confirmation for this flash; serial
initialization does not establish them.

# Publication follow-up, 2026-10-13

The pending implementation and evidence were rebased onto current main and
published as [implementation PR 67](https://github.com/jrepp/jelligothci/pull/67)
and dependent [documentation PR 68](https://github.com/jrepp/jelligothci/pull/68).
The memo-066 collision with upstream Studio documentation was resolved by
renumbering the local LVGL dispatch record to [memo-071](memo-071-lvgl-dispatch-and-empty-overlay-trial.md),
preserving its UUID and created date. The index conflict retained both entries.

On the rebased tree, desktop/core/sanitizer suites passed 74/33/74 tests;
41 targeted Python tests, all repository hooks, documentation validation and
repair, all nine firmware builds, profile isolation and cache-matrix checks
passed. The rebuilt normal image reports v0.11.0; it was not flashed. These
checks do not extend the physical evidence for the earlier v0.8.0 deployment.
Logs remain under ignored `build/pr-publish/`.

Updating the first PR's cross-link with `gh pr edit` failed because its GraphQL
query requested deprecated Projects classic metadata. A REST PATCH through
`gh api`, with the exact body in a JSON input file, succeeded; subsequent reads
confirmed both PRs were open and mergeable with their intended bases. CI was
still pending at publication. The installed CLI was not upgraded.

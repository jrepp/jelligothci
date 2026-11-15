---
title: ESP-IDF 6.1 profile validation
author: Engineering Team
created: 2026-11-15T16:59:52Z
tags: [firmware, toolchain, validation]
id: memo-118
project_id: jelligotchi
doc_uuid: ff1cacf5-b203-432c-a0ee-593471e58370
---

# Scope

Update the [pinned SDK](../../toolchain.env), [component constraint](../../ports/esp32/main/idf_component.yml)
and [resolved dependencies](../../ports/esp32/dependencies.lock) to ESP-IDF 6.1.
The board and BSP remain those named in the [hardware guide](../../docs/hardware.md).

# Evidence and limits

A clean main-based worktree built the normal, factory and power profiles using
the repository-local SDK pin. The profile isolation checker passed for each.
An incremental normal build was also run. These builds establish compilation
and profile separation, not peripheral timing or physical behavior.

A separate reconciliation worktree containing additional gameplay and power
changes was built and flashed to the attached board with verified transfer
hashes. Serial startup reported application 0.19.0, passing PSRAM, panel creation,
touch registration and game readiness. The debug endpoint reported an available
save. That image is not this standalone SDK branch, and physical display, touch,
audio and power behavior were not established by the startup log.
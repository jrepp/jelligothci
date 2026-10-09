---
title: Agent phase 0, two apps on one board and the first hardware probes
author: Claude
created: 2026-10-09T14:41:10Z
tags: [agent, esp32, flash, hardware, memo]
id: memo-024
project_id: jelligotchi
doc_uuid: 0f6e2f10-a004-4e01-992c-37ca021b4472
---

# Overview

The user said "go" to phase 0 of
[RFC-003](../rfcs/rfc-003-voice-agent-companion.md): spikes and measurements before
the voice agent's MVP. This memo records the first part. Both apps now live on one
board ([ADR-013](../adr/adr-013-two-apps-on-one-board-with-flash-layout-a.md)), the
agent app exists as a skeleton, and its read-only probes answered the RFC's
hardware questions. The Wi-Fi and audio spike and the render spike come next.

# Context

- Board: Waveshare ESP32-S3-Touch-AMOLED-1.75C, MAC 7C:0C:5F:42:33:54, on
  `/dev/cu.usbmodem101`. Before this work it ran the pet at commit 15978d7 with
  the 8 MB single-app layout of ADR-012.
- Before any flash, the save region (`nvs`, 0x9000, 24 KB) was read back to
  `build/device-backup/7C0C5F423354-nvs-before-agent-phase0.bin` (SHA-256
  021eb508…c5c4), and the pet's debug state was saved under `build/phase0/`. Both
  are ignored by Git.

# Details

## What was built

- **Layout A** in `ports/esp32/partitions.csv`: pet in `factory` (7 MB), agent in
  `ota_0` (4 MB), `otadata`, `agent_nvs`, and reserved `model` and `art`
  partitions. Both projects enable bootloader app rollback.
- **Pet**: an `app agent` debug command. The core gates it (busy while a capture or
  an offline catch-up runs) and requests a checkpoint. The ESP32 host accepts only
  a `jelli_agent` image in `ota_0`, waits for the checkpoint and the reply, then
  selects `ota_0` (a full image verification) and restarts. The SDL host has no
  switch and answers `app_unavailable`.
- **Agent** (`ports/esp32-agent`, project `jelli_agent`, 240 MHz): an `@A1` USB
  console framed like the pet's `@J1` protocol, with the parser in portable C
  (`core/agent/console.c`). Commands are `hello`, `probe chip|heap|pmu`,
  `probe pwr MS`, `probe te MS`, and `boot pet`. The identity screen is a lilac
  ring drawn straight to the panel without LVGL: 16-row stripes from one static
  14,912-byte DMA buffer, big-endian RGB565, waiting for each transfer's
  completion. The agent confirms its trial boot once its console runs.
- **Tooling**: `JELLI_ESP_APP=agent` for `scripts/esp`, `flash-agent` (parttool
  writes `ota_0` by name), `boot PORT pet|agent` (otatool), `make esp-agent-build`
  and `esp-agent-flash`, and `scripts/jelli-agent`.
- **Board pins** the BSP does not expose, in the 1.75C wrapper only: PWR key level
  on GPIO3 and the panel's tearing-effect output on GPIO13. On the 1.75 these
  probes answer `unsupported_board`.

## Hardware verification, 1.75C, 2026-10-09

| Step | Evidence |
| --- | --- |
| Pet flashed with layout A | Bootloader, table, `factory`, and `otadata` written with verified hashes. The boot log lists eight partitions and "Defaulting to factory image"; session save available; 61 Hz engine updates |
| Save kept | Same four pets, active unicorn, bond 135, food 2, gifts 3, reward pending. Needs drifted only with elapsed time |
| Agent written to `ota_0` | 312,512 bytes at 0x710000 by partition name, hash verified; the pet kept running |
| Pet to agent (`app agent`) | Reply `restart: true`; the agent answered `hello`; `probe chip` reported `ota_0`, image state `valid` (trial confirmed) |
| Agent to pet (`boot pet`) | Pet state unchanged (bond 135, four pets) |
| Host switch both ways | `boot … agent` started the agent with image state `undefined` (no trial); `boot … pet` erased `otadata` and the pet resumed with its save |
| Rollback | A temporary local agent build that never confirms ran as `pending_verify`. After a reset the bootloader started the pet. The real agent was then rewritten to `ota_0` |

## Review

The security review passed with conditions: no memory-safety defect. Three
findings, each confirmed by two skeptic reviews at low severity, were fixed:

- `flash-agent` now resets the boot choice to the pet before writing `ota_0`, so a
  rebuilt agent never starts as already confirmed and cannot boot-loop.
- Both builds stop with `#error` when a stale generated `sdkconfig` has rollback off.
- The agent confirms its trial after start-up and before its console task starts,
  so "boot pet" cannot race the confirmation's rewrite of `otadata`.

Recorded as notes, not fixed: `scripts/esp idf … flash` with `JELLI_ESP_APP=agent`
would write the agent over the pet; `scripts/esp boot PORT agent` checks no project
name and skips the trial; neither switch checks the board variant (a 1.75 agent
on a 1.75C passes the name check); and the pet answers `app agent` before the final
full-image verification, so a damaged image only shows in the log.

## Probe results

| Probe | Result | Meaning for RFC-003 |
| --- | --- | --- |
| `chip` | ESP32-S3 revision 0.2, 2 cores, 240 MHz; JEDEC `c84019`; 32 MiB flash; 8 MiB PSRAM | The 1.75C as documented (GigaDevice 32 MB flash) |
| `heap` (skeleton, display on) | Internal free 333,963 B (minimum 297,404; largest block 241,664); DMA-capable 326,175 B; PSRAM 8,386,192 B | Baseline before Wi-Fi. The RFC expects Wi-Fi to leave 184–274 KB, which fits |
| `pmu` | 0x22 = 0x06, 0x27 = 0x14, 0x90 = 0xFF, 0x92 = 0x1C | ALDO1 (microphone supply) is on at 3.3 V, so audio needs no PMU write. Holding PWR for 6 s powers off; the long-press IRQ fires at 1.5 s; power-on needs 128 ms; over-temperature shutdown is on |
| `te 2000` | 118 rising edges on GPIO13: 59.0 Hz | The panel refreshes at about 59 Hz and its TE pulse is usable for tear-free presents |
| `pwr 1000` (idle) | GPIO3 low for all 500 samples | The key line is readable; the pressed level is not yet observed |

A PWR click shorter than 1.5 s is therefore a safe "cancel" gesture, as the RFC
assumed, and the 6 s power-off leaves room for it.

## Re-flash incident

The first version of the `flash-agent` fix ran ESP-IDF's otatool, which connects
to the board several times and lets esptool hard-reset after each call. With the
agent confirmed and running, the third connection failed ("No serial data
received"), so the script stopped before writing anything. After that the board
answered nothing on USB: no boot log after an RTS reset, no esptool connection with
default, `usb_reset`, or `no_reset` entry, and no console reply, although the USB
device stayed enumerated (303A:1001) and no process held the port. It matches the
earlier silent board 28:84:85:B0:8A:38. The battery keeps the chip powered, so
unplugging USB alone may not reset it; recovery needs a power cycle (hold PWR 6 s).

Remedy in the tooling: `flash-agent` and `boot` now pass `after=no_reset` to every
otatool and parttool call, so the chip stays in its download mode between steps
and no app boots under the next connection, then reset once at the end. Flash is
unchanged by the failed attempt (nothing was erased or written), so after a power
cycle the confirmed agent starts. Pending: the new sequence on hardware, and
whether rapid resets alone can wedge the USB-Serial/JTAG link.

# Remaining uncertainty

- The identity screen (lilac ring at 60% brightness) is not yet confirmed by eye.
- The PWR key's pressed level needs someone to press it during `probe pwr`.
- The 1.75 builds both apps but was not flashed; its PWR and TE pins are unknown.
- The trial boot protects only switches made by the pet; the host switch with
  otatool skips it.

# Next

1. Wi-Fi and audio spike: ES7210 TDM capture at 16 kHz (MIC1 plus the MIC3 echo
   reference) streamed to the Mac over a WebSocket, and a Chatter reply played on
   the ES8311.
2. Render spike: a layered 384 px unicorn with dirty-rectangle presents aligned to
   the 59 Hz TE signal, measured at 240 MHz.

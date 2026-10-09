---
title: Screen sleep after three idle minutes and double-tap wake
author: Claude
created: 2026-10-09T02:57:15Z
tags: [esp32, memo, power, ui]
id: memo-023
project_id: jelligotchi
doc_uuid: 09cae34d-d6a8-48d7-8bb8-7b6e65f56732
---

# Overview

The user asked to "add the ability to sleep the screen of the board until the user
double taps it to wake it up. Have it go to sleep after 3 minutes of idle time."
This memo records how screen sleep works, how hosts and the debug interface use
it, and what was verified. Player-facing behavior is in
[playing.md](../../docs/playing.md#screen-sleep); the board details are in
[hardware.md](../../docs/hardware.md#hardware-bring-up).

# Context

"Screen sleep" means the display is off. It is a different thing from the pet's
own sleep (`JelliPet.asleep`, Settings REST/WAKE), and the code keeps the two
apart: the screen state is `JelliPetScreen` with `awake`/`asleep` names under
`jelli_pet_screen_*`, and nothing about the pet's sleep changed.

# Details

## Policy

The platform-independent policy is in `include/jelli/pet_screen.h` and
`core/pet_screen.c`, a 40-byte fixed state inside `JelliPetEngine`:

- Every polled `TAP`, `SWIPE`, and `TOGGLE_PAUSE` while awake restarts the idle
  timer. After `JELLI_SCREEN_SLEEP_MS` (180,000 ms) of the engine's injected
  clock without such input, the screen sleeps. `QUIT` is never activity.
- While asleep, taps, swipes, and pause toggles never reach the UI. Two taps at
  most `JELLI_SCREEN_DOUBLE_TAP_MS` (500 ms) apart and within
  `JELLI_SCREEN_DOUBLE_TAP_PX` (80 px, Euclidean, inclusive) wake the screen.
  Neither waking tap is delivered. A tap that does not pair becomes the new
  candidate, so the window restarts from the latest tap; a swipe cancels the
  candidate.
- Addition beyond the agreed design: for 500 ms after a double-tap wake, further
  taps are swallowed (still counted as activity). Without it, a triple tap on a
  dark screen would press whatever button lies under the third tap. A forced
  (debug) wake does not arm this guard.
- Time handling: a clock that regresses re-anchors the idle timer rather than
  underflowing; a large forward jump counts as idle time; no times are added, so
  values near `UINT64_MAX` cannot wrap. Distance checks bound each axis before
  squaring, so any `int` coordinates are safe.

## Engine

`jelli_pet_frame` runs rewards, game advance, and input exactly as before. The
input loop routes each polled input through the screen gate, then applies the
idle rule, then updates the gallery. While asleep it returns before rendering:
no render, no damage, and no `present`. Waking sets `ui.rendered = false`, so the
next frame is a full redraw of the unchanged UI page and menu; a test checks it is
pixel-identical to a fresh render of the same state.

Hosts learn the state through a query rather than a new `JelliPlatform`
callback. Appending a callback would change the shared shapes contract and every
positional platform initializer (`-Wmissing-field-initializers` under `-Werror`).
`jelli_pet_screen_changed(engine, &awake)` is edge-triggered: it returns true once
per change since the previous call, and a sleep then wake between two calls
coalesce into no change. `jelli_pet_screen_awake` reads the current state.
`jelli_pet_sound_cue(engine, now)` replaces the hosts' direct
`jelli_pet_ui_sound` calls: silent while paused (nothing consumed, as before) and
while the screen sleeps (cues consumed and discarded, so no coo bursts on wake).

## Debug interface

`@J1 <id> screen` reports `{"awake","sleep_in_ms","sleep_after_ms"}`;
`screen sleep` and `screen wake` force transitions (busy during a capture; other
arguments return `screen_action`). `state` carries the same object as
`visual.screen`; the rest of `visual` remains the last drawn frame. `capture`
returns `screen_asleep` while dark, because the framebuffer is stale. Debug
`press`, `tap`, and `swipe` count as trusted developer activity: when accepted
they restart the timer, wake a dark screen directly, and deliver their input.
Rejected debug input (range, busy, stale page) changes nothing. Other debug
commands are not activity. The command fits ADR-010's bounds (four words, a
128-byte body); `JelliDebug` did not grow. The CLI adds
`jelli-debug screen [sleep|wake]`.

## Hosts

ESP32: on sleep the host clears the LVGL canvas to black, invalidates it, and
sets brightness 0 with `bsp_display_brightness_set`, all under the BSP display
lock (brightness writes share the panel IO with LVGL flushes). On wake it restores
brightness 60 after the engine's full redraw has been copied. The loop waits 50 ms
instead of 16 ms while dark. LVGL keeps reading the separate CST9217 touch
controller and the engine keeps draining the queue. Power/PMU, light sleep, and
Wi-Fi are unchanged. Taps are stamped at poll time, so a measured gap can exceed
the physical gap by up to one loop interval.

SDL: on sleep the window is cleared to black once, presented, and titled
`screen asleep | double-click wakes`; the engine stops presenting. A double-click
(two left clicks) produces two taps and wakes it. Transitions are logged to stderr.
Headless runs use the virtual 8 ms-per-frame clock, so they stay deterministic.

## Verification

Desktop (macOS): `make test` passed 64 of 64 tests (61 before, plus `pet_screen`,
`pet_screen_engine`, and `debug_screen`); `make core-test` passed 24 of 24;
`make sanitize` passed 64 of 64; `./scripts/c-lint check` and
`./scripts/c-lint self-test` (5 tests) passed. The engine test runs a dark and a
lit engine side by side for 15 simulated minutes and finds identical encoded
games, event logs, ticks, and animation time, with no presents or cues from the
dark one. All existing suites passed without scenario changes.

Headless SDL: `--frames 22500` (last frame at 179,992 virtual ms) stays awake;
`--frames 22501` logs `Screen asleep`. An instance with its own `--save` and
`--debug-socket` under `build/screen-demo/` fell asleep after three idle minutes
of its virtual clock. While dark, ticks kept advancing, `screen` reported
`awake:false`, and `screenshot` failed with `screen_asleep`. `screen wake` and a
single debug `tap` each woke it, and a screenshot after waking showed the full
home scene. The real double-click path was not driven in SDL, because the
headless dummy video driver cannot receive mouse input; the core tests cover the
polled-tap path.

Firmware: `make esp-build` (1.75) produced `jelligotchi.bin` of 0x576d70 bytes
(5,729,648; 32% of the 8 MB app partition free), and
`JELLI_ESP_BOARD=1.75c make esp-build` 0x5743c0 bytes (5,718,976; 32% free).

# Recommendations

Still unverified on hardware:

- Whether the AMOLED is fully dark at brightness 0 with a black canvas, and how
  much current screen sleep saves.
- Touch and double-tap wake while the panel is dark, and the wake latency at the
  50 ms loop interval.
- Any visible flash at wake: the canvas is redrawn before brightness returns, but
  LVGL flushes asynchronously.

Verify these on a board after an authorized flash, and record the measurements
here.

# References

- [Playing: screen sleep](../../docs/playing.md#screen-sleep)
- [Hardware bring-up](../../docs/hardware.md#hardware-bring-up)
- [ADR-010: bounded debug interface](../adr/adr-010-bounded-usb-serial-debug-interface.md)
- [memo-021: sound level](memo-021-settings-sound-level.md)

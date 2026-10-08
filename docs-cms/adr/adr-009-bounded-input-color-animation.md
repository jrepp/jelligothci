---
id: adr-009
title: Add bounded color transitions driven by injected input and time
status: Accepted
created: 2026-10-08
deciders: Jacob Repp
tags: [animation, input, memory, timing]
project_id: jelligotchi
doc_uuid: a6012c67-b49d-4e86-ad49-43a6944dd61a
---

# Context and approval

Jacob requested visible reactions to local clicks and ESP32 touch, a simple
animation system with color changes bounded to 0.3 seconds, and faster updates.
This extends the shapes MVP without adding creature behavior or persistence.
The earlier tap-to-pause interaction in ADR-003 describes the initial version.

# Decision

A press anywhere in the visible round surface cycles all three shapes through
fixed palettes over 300 ms. Shape-specific hit testing is outside this increment.
Keep Space as a desktop motion pause toggle. Color feedback continues while
motion is paused; tapping no longer toggles pause.

Use a portable integer RGB tween with caller-owned storage and injected elapsed
time. The engine owns three fixed slots and a palette index. Retarget an active
tween from its current value without discontinuity; the latest target replaces
the previous target. No allocation or pending-animation queue is introduced.
The current layout uses 20 bytes per tween, 60 bytes for three slots, plus the
palette index, renderer history, damage metadata, and structure padding. Existing framebuffers remain
434312 bytes each; the two ESP32 buffers still total 868624 bytes.

A zero-duration tween snaps immediately. Otherwise clamp elapsed time to duration
before interpolation. Large time jumps finish in constant work; exact endpoint
colors remain stable. RGB interpolation is linear in channel values, not gamma
corrected. RGB565 presentation quantizes the interpolated values.

Hosts continue to own pacing. SDL targets 8 ms instead of 16 ms; ESP32 targets
16 ms including frame work instead of an extra 33 ms sleep. Set the LVGL display
refresh timer to 16 ms under the BSP mutex. Yield at least one tick on an ESP32
overrun, with no catch-up burst. Requested update rate is not guaranteed panel
throughput; log the observed engine rate and sampled frame costs on hardware.

Input work remains bounded: the engine accepts at most 32 logical events per
frame; each SDL poll scans at most 32 raw events. ESP32 keeps its eight-event
queue and drops the incoming event if full. Touch still crosses from the LVGL
task to the engine through that queue; the two-buffer ownership model is retained.

The surface now reports a bounded dirty rectangle. The host preserves pixels
between frames; the first frame initializes the full buffer. The core clears and
redraws the union of old/new shape bounds, including the stationary shapes while
colors change. Zero damage means no pixel changes. SDL updates that texture
region; ESP32 copies those rows under the display mutex and invalidates that
canvas area. Renderer history must stay paired with its persistent buffer.
No third framebuffer or heap-backed damage list is introduced.

# Implementation findings

Initial board profiling with debug compiler optimization observed about 4 Hz,
with a frame sample around 103 ms rendering and 105 ms in presentation (including
lock wait). A shorter requested interval alone did not make rendering fast.
Select the SDK's performance optimization in tracked defaults while retaining
assertions; verify the actual generated configuration when upgrading an existing
checkout. Compiler settings are an implementation choice for the requested
performance work, not an increase to the CPU clock or a change to power policy.

# Validation

Fake-time tests cover start/midpoint/299 ms/300 ms, retarget continuity, cadence
independence, zero duration, maximum duration and elapsed values, paused motion,
invalid taps, RGB565 output, and bounded input draining. Incremental-render tests
compare against full redraws across motion, palette changes, and large jumps;
they check untouched pixels, padding, damage bounds, and unchanged frames. Run desktop/core tests,
sanitisers, C checks, and firmware compilation for this shared-engine change.
Record deployment and measured hardware behavior separately from requested rates.

# References

- [Injected timing](adr-002-inject-time-and-keep-pacing-in-hosts.md)
- [Initial scope](adr-003-start-with-a-shapes-mvp.md)
- [Tween contract](../../include/jelli/animation.h)
- [Animation tests](../../tests/test_animation.c)

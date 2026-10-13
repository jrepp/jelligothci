---
title: Display command stack margin qualification
author: Codex
created: 2026-10-11
tags: [embedded, performance, validation]
id: memo-069
project_id: jelligotchi
doc_uuid: 59c9af53-500b-4e33-8646-cd5835b3bb26
---

# Scope and finding

On the user's instruction to continue [memo-068](memo-068-display-startup-order-and-cache-results.md),
this increment addresses its main-task stack concern before further cache
qualification. It does not promote cache settings or change their performance
claims. The working tree is based on
`ab2b6f73364b3b44d36f91ca13b2e12a0f450e0b`, including the earlier uncommitted
hardware trials.

Disassembly of the pinned Xtensa baseline image showed that
`jelli_display_output_command` reserved 448 stack bytes even when forwarding
to experiment or network handlers. Its own health formatting buffer and inlined
copy benchmark increased the dispatcher frame beneath nested handlers. The
profiling report separately reserved 480 bytes. These are compiler frame sizes,
not whole-call-chain bounds or a diagnosis of every historical low-water mark.

[Display command handling](../../ports/esp32/main/display_output.c) now puts
health formatting in a separate helper and uses the SDK's `NOINLINE_ATTR` on
that helper and the copy benchmark. The explicit ESP32-only boundaries prevent
those buffers from being incorporated into the dispatcher. The resulting
dispatcher frame is 48 bytes; the health helper is 384 bytes and the copy
benchmark is 336 bytes. There is no added static scratch, heap allocation,
stack reservation, task, queue, framebuffer or DMA ownership change. JSON
responses and diagnostic behavior are preserved.

# Device evidence

Discovery found the intended SKU 31261 board at `/dev/cu.usbmodem2101`, USB
serial `90:70:69:FE:21:DC`. The pre-change baseline image matched the earlier
memo-068 baseline ELF identity. Each capture verified flash/boot identity as
applicable, PSRAM/panel/touch startup, deferred LVGL worker startup, Wi-Fi
initialization and ten seconds of warmup. The existing pipeline station used
baseline cache settings, original memory/copy/overlay modes, ten-second captures
and forced full refresh every 500 ms. Networking was initialized successfully
but disabled and disconnected throughout.

| Capture | Main-task stack headroom | LVGL update p95 |
| --- | --- | --- |
| Before change | 752 bytes | 84.360 ms |
| After change, first boot | 1072 bytes | 84.642 ms |
| After change, second boot | 1152 bytes | 84.579 ms |

Fresh health reads after waiting beyond the periodic display-check interval
confirmed the recorded headroom. Main-task headroom increased by 320–400 bytes
in these runs, consistent with the compiled dispatcher reduction. Boot-to-boot
variation remains; this is not a guaranteed minimum for arbitrary workloads.
It does not measure the LVGL worker's stack or reproduce every earlier low
reading. The timing observations show no material change in this small sample;
they are not a new rendering speedup or a statistical equivalence test.
All captures passed buffer equality, guards, heap integrity, network state,
transfer health and mode-restoration checks.

During this continuation the user confirmed that display colors/animation and
taps looked correct on the previously installed normal firmware. That operator
observation qualifies the pre-change normal image from memo-068; it does not
qualify experimental cache images or establish physical verification of the
subsequent firmware deployment.

# Validation and remaining work

Normal, power, factory and all six cache configurations built through the pinned
wrapper. Profile isolation and the complete cache configuration matrix passed;
the build manifest was regenerated for the current source. C formatting,
Clang-Tidy, Cppcheck and size checks passed, along with 24 existing pipeline,
factory and cache-checker tests. No portable core or wire contract changed.

Normal firmware with original cache settings and both startup/stack fixes was
restored. Flash hashes and the running ELF identity (`f3c5537ef` prefix)
matched. Version 0.8.0, the deliberate USB reset, successful PSRAM test,
panel/touch initialization, deferred worker startup and application readiness
were observed without SPI/Wi-Fi allocation failures. Fresh post-recovery health
checks reported 1156 bytes of main-task headroom, zero mismatches/transfer
failures and intact buffer equality, guards and heap. Serial connections were
closed after observation. A subsequent normal-image smoke check exercised
capabilities, display reporting, the isolated copy benchmark, forced refresh
and network reporting. The benchmark's equality/guard check passed; fresh
health afterward remained healthy and reported 980 bytes of main-task headroom.
This lower value illustrates why the pipeline-only reading is not a universal
stack bound.

Documentation validation and repair passed with no repairs or unresolved
findings. Whitespace checks passed.

The fastest cache configurations remain experimental. Connected-network memory
load, reduced-instrumentation timing, final draw-buffer/panel equivalence and
physical candidate display/touch checks remain outstanding. No new network
credentials, power policy or gameplay changes were introduced.

Raw startup, flash, capture and health logs, source snapshots, identities and
compiled-frame evidence are retained under ignored `build/stack-device-001`;
the original disassembly is under `build/cache-device-002`.

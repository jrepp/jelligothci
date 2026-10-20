---
title: Allocation audit and bounded-memory improvement trajectories
author: Codex
created: 2026-10-19
tags: [audit, memory, portability, resources]
id: memo-071
project_id: jelligotchi
doc_uuid: 787723c1-b45f-48cb-97c0-ad0e7b37693f
---

# Scope and evidence

The owner requested a Luna source audit followed by Sol recommendations for
arena, stack, pool, and static allocation strategies. The audit inspected
`core`, `include`, and SDL/ESP32 adapters at source revision
`d830df7374ca3e72ed0608f6341b0776d0ed29e0`, with the subsequent Jellipak design
still uncommitted. No behavior was changed, firmware flashed, or runtime heap
trace collected for the audit.

No explicit dynamic allocation calls were found in the portable engine.
The [engine surface contract](../../include/jelli/engine.h) borrows host-owned
memory; core state uses fixed arrays and caller-provided storage. This is source
evidence, not a claim that every callback, libc function, or SDK dependency is
allocation-free.

# Allocation inventory

The byte counts below are dated calculations from the audited declarations,
not measured device heap deltas. SDK bookkeeping, alignment, and additional
library-managed allocations are excluded.

| Site | Lifetime and observed ownership | Source-derived payload |
| --- | --- | --- |
| [SDL framebuffer](../../ports/sdl/main.c) | Startup allocation, borrowed by renderer, freed during cleanup | 434,312 bytes |
| [SDL live artwork workspace](../../ports/sdl/asset_reload.h) | Optional startup allocation; two decoded banks and staging retained until exit | Use target `sizeof(JelliAssetReload)`; descriptor layout is ABI-dependent |
| [ESP framebuffers](../../ports/esp32/main/display_output.c) | Startup PSRAM allocations; separate engine and LVGL canvas ownership | 868,752 bytes combined, including guards |
| [ESP transfer strip](../../ports/esp32/main/display_transfer.c) | Startup aligned internal DMA allocation; reuse only after transfer completion | 46,600 bytes |
| [Input queue](../../ports/esp32/main/main.c) | Fixed-capacity startup RTOS allocation, application lifetime | Payload and control overhead depend on target types/SDK |
| [Network queues, event group, task](../../ports/esp32/main/network.c) | Startup objects; partial failures delete created queues/group | Fixed capacities; task stack argument follows ESP-IDF byte semantics |
| [Factory scratch](../../ports/esp32/main/validation/factory/suite.c) | Factory-profile PSRAM probe retained for test lifetime | 1,024 bytes |

The [audio adapter](../../ports/esp32/main/sound_output.c) already supplies static
queue, mutex, task, stack, PCM, and synthesizer storage. Its codec and I2S
dependencies remain separate allocation owners. Inspection of the local pinned
ESP-IDF headers confirmed byte-based task stack arguments for this target;
the existing static audio stack was not identified as defective. Do not import
vanilla FreeRTOS stack-unit assumptions into this adapter.

The [network initialization](../../ports/esp32/main/network.c) cleans up partial
queue/event creation failures. Framebuffer and DMA allocation failures are
fatal startup errors. Those policies are current behavior, not evidence of
observed allocation failures.

# Runtime allocation uncertainty

[SDL reload](../../ports/sdl/asset_reload.c) and
[storage](../../ports/sdl/storage.c) use stdio during runtime. Their application
buffers are bounded, but `FILE` and buffering allocation is library-managed.
[Network polling](../../ports/esp32/main/network.c) can call `setenv`/`tzset`
when the timezone changes; this is a potential libc allocation on the engine
host thread. Moving it requires an ownership policy for process-global timezone
state, not merely changing which thread calls it.

BSP/LVGL, Wi-Fi, SNTP, HTTPS/OTA, codec/I2S, and NVS own additional resources.
Their initialization and event-time calls do not reveal all allocation sizes
or lifetimes in application code. A source search cannot establish their
steady-state behavior. Future tracing should distinguish startup, warm frames,
pet/scene transitions, save operations, audio enable/disable, network changes,
and OTA.

# Recommended trajectories

The proposed foundation and adoption sequence are recorded in
[RFC-009](../rfcs/rfc-009-bounded-memory-systems-and-adoption.md).

1. Preserve the portable core's caller-owned memory boundary. Add checked
   layout and bounded storage helpers only when a consuming system needs them.
2. Use dedicated worker scratch for decompression; large compressed/indexed
   buffers do not belong on a task stack.
3. Evaluate complete-generation arenas against a pinned payload pool for
   Jellipak residency. Two arenas require full current-plus-next capacity;
   shared pools can retain common assets but must bound fragmentation.
4. Use static metadata, job/completion slots, and small fixed control objects.
   Static RTOS conversion is an optional measured cleanup, not a prerequisite
   for the asset system.
5. Keep justified PSRAM and DMA startup allocations. Replacing them with
   ordinary static arrays could move large payloads into scarce internal RAM.
6. Replace desktop's full-catalog bank model through the shared Jellipak
   working-set design, preserving atomic reload and source lifetimes.

A generation number detects stale handles; it does not keep their bytes alive.
An arena cannot reset while anything still leases its storage. Cancellation
must be acknowledged before its destination memory or source can be reused.
Pool admission must check contiguous capacity as well as total free bytes.

# Verification and remaining work

The audit and strategy review are complete; the allocator/runtime improvements
are proposals. Later implementation needs capacity/overflow/stale-handle tests,
transaction rollback and pinning tests, deterministic and actual-thread worker
tests, and source/candidate lifetime checks before adoption.

Measure exact target layouts, capability-specific heap minima and largest
blocks, worker/main stack high-water marks, transition peaks, and frame/input
latency. Keep host tests, firmware compilation, and physical display/touch
evidence distinct. Allocation strategy improves RAM predictability; the
[compression measurements](memo-070-compressed-resource-payload-measurements.md)
and matched linked firmware builds determine flash savings.
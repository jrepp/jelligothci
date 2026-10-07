---
name: jelli-embedded-development
description: Implement or review Jelligotchi firmware, ESP32 board integration, memory budgets, and timing or task boundaries. Use for embedded behavior and hardware bring-up; not for documentation-only work or unrelated desktop features.
---

# Embedded development for Jelligotchi

Keep work within the requested increment. Start with the relevant ADRs in
[docs-cms](../../../docs-cms/README.md), the [platform contract](../../../include/jelli/engine.h),
and the existing port. The target is Waveshare ESP32-S3-Touch-AMOLED-1.75,
SKU 31261. Do not substitute settings for the 1.75C or another board revision.

## Allocate a budget before adding work

For changes that affect resources, account for persistent storage, task stack,
scratch space, queue capacity, and bytes transferred per frame. Distinguish
internal RAM, PSRAM, DMA-capable memory, and flash; they are not interchangeable.
Use byte counts from actual types and dimensions rather than guessed capacity.

The current RGB565 frame uses 466 * 466 * 2 = 434,312 bytes. The ESP32 host has
one engine buffer and one LVGL canvas buffer, totaling 868,624 bytes before BSP
buffers, tasks, queues, and driver allocations. Do not call that the total RAM
footprint. Keep large framebuffers off task stacks.

Prefer fixed storage, caller-supplied buffers, and bounded pools. The core and
steady-state engine loop must not allocate or free heap memory. The current
ESP32 port and SDK allocate at startup, including PSRAM frames and a queue;
do not describe the whole firmware as allocation-free. When changing these
allocations, consider static or caller-owned storage first. If a platform API
requires startup allocation, keep size/count bounded, handle failure, explain
why it is needed, and avoid allocation churn in callbacks and frame loops.

## Preserve platform and concurrency boundaries

- Keep hardware setup and SDK types in the host. Inject clocks, input, output,
  and drawable memory into the core; do not poll peripherals there.
- Use the existing BSP and pinned headers to verify APIs and pin assignments.
  Investigate configuration or transport errors before replacing a driver.
- Document buffer owner and lifetime across each task, queue, and DMA transfer.
  For queued pointers, ensure the producer cannot reuse the memory too early.
- The current `present` contract finishes reading/copying before returning.
  Preserve its separate LVGL buffer and display mutex unless replacing them
  with another explicitly synchronized ownership scheme.
- Bound queues and event-draining work. Define overflow behavior. Avoid
  unbounded catch-up loops after stalls, blocking I/O in a frame callback, and
  lengthy work while holding display locks.
- Use task/ISR APIs in their intended context. An ISR should acknowledge or
  enqueue bounded work; defer logging, allocation, and rendering to tasks.
  `volatile` is not task synchronization or a substitute for atomics/locks.
- Keep time units explicit. Distinguish elapsed monotonic time from wall time,
  and define rollover, sleep, and resume behavior when the feature needs it.

## Verify at the layer changed

Use the repository's CMake/Make workflow and pinned `scripts/esp`; do not add
implicit SDK upgrades or global environment changes. Run host tests for shared
logic and `make esp-build` for affected firmware. `make hooks-check` covers
formatting, static analysis, size limits, and docs; it does not prove timing,
DMA safety, or electrical behavior on hardware.

When the task includes device work, discover its port with `scripts/esp ports`.
Keep build and flash distinct. If flashing is authorized, identify the intended
board and image first. Do not extend a display task into partition erasure,
eFuse changes, or charging/power configuration without task authorization.
Stop and diagnose repeated flashing or boot failures instead of escalating to
those unrelated mutations.

Report observed evidence: desktop tests, firmware build, physical display/touch,
and any measured stack/heap/frame-time data. If no device test was performed,
say so. Save durable findings in a memo; record new architectural choices as
ADRs with status reflecting the user's actual decision.

---
id: memo-006
title: Input animation and measured frame performance
author: Jacob Repp
created: 2026-10-08T00:13:51Z
tags: [animation, embedded, memo, performance, validation]
project_id: jelligotchi
doc_uuid: b303e8b7-3bd9-400c-ac02-b7126e8c6e55
---

# Outcome

The input MVP cycles all three shapes through fixed palettes on click or touch.
Transitions use injected time and finish at 300 ms, with fixed storage and no
queued animations. Space pauses desktop motion without pausing color feedback.
The engine and both hosts share the same input and tween implementation.

# Performance investigation

Reducing sleep alone did not achieve the requested faster updates. Serial
profiling on the attached ESP32-S3 at 160 MHz produced these observations:

| Configuration | Observed engine rate | Sample render / present |
| --- | --- | --- |
| Debug optimization, full-frame update | 4 Hz | 103 / 105 ms |
| Performance optimization, full-frame update | 7 Hz | 35 / 99 ms |
| Performance optimization, dirty-region update | 61–62 Hz | 1 / 3 ms |

Rates are five-second engine-loop measurements during motion, not measured panel
scanout FPS or touch latency. Sample costs are individual frames, not percentiles;
unchanged pixels can produce a zero-cost sample. Color transitions cover a larger
region and were not physically exercised during this measurement. The 300 ms
logical completion is visible at the next available presentation.

The tracked SDK defaults now request performance optimization. Assertions remain
enabled and CPU frequency remains 160 MHz. Existing generated sdkconfig values
can override new defaults: inspect CONFIG_COMPILER_OPTIMIZATION_PERF and change
the existing choice through the local SDK configuration workflow when upgrading.
Do not delete unrelated local configuration merely to adopt one new default.

The renderer preserves its framebuffer and updates a bounded rectangle. Hosts
copy/upload that rectangle; ESP32 invalidates it in the LVGL canvas. The display
mutex and separate buffers preserve ownership across the engine and LVGL tasks.
The two application PSRAM frames still total 868624 bytes. Three tweens add 60
bytes plus palette/history/damage metadata and padding; the input queue remains
eight JelliInput events. No additional framebuffer or dynamic animation queue
was added. Motion usually transfers about 10 KB of canvas pixels per frame,
compared with 434312 bytes for a full frame; first render remains full-frame.

# Validation and deployment evidence

- Desktop, core-only, and ASan/UBSan tests passed with warnings as errors.
- Fake-time tests exercise completion, retargeting, cadence independence, pause,
  maximum counters, invalid coordinates, and bounded input draining.
- Incremental frames match full redraws across a full motion cycle, color changes,
  and large jumps; padding and pixels outside reported damage remain untouched.
- C formatting, clang-tidy, Cppcheck, and size/complexity checks passed.
- Firmware compiled with the pinned ESP-IDF v5.5.5 and was flashed to the discovered
  USB JTAG/serial board (303A:1001, serial 90:70:69:FE:21:DC); transfer hashes passed.
- Serial startup reported PSRAM test success, CO5300 panel and CST9217 touch
  initialization, LVGL startup, and the new tap-for-color-transition ready message.
- Application descriptor still reports 0.1.1 because release automation owns the
  next version bump. This is an unreleased development build on that version base,
  not the previously published 0.1.1 artifact. Observed ELF SHA prefix: 4a6f95d66.
- A deterministic SDL snapshot was visually inspected for shape/mask correctness.
  Physical touch/color/smoothness confirmation remains pending from the user.

Local raw evidence is ignored build output: input-mvp-serial.log,
input-mvp-profile.log, input-mvp-perf.log, and input-mvp-damage.log under build/.
The final serial sample showed repeated 62 Hz reports. No panic was observed;
the monitor's USB_UART_CHIP_RESET was intentional. Existing panel initialization,
I2C pull-up, and disabled-gesture advisories remained; touch registration succeeded.

# Reusable process

Profile rendering and presentation separately before tuning delays. Distinguish
requested frequency, engine throughput, display throughput, and physical input
verification. Test incremental output against a full redraw and validate its
damage contract before optimizing host transfers. Keep a durable summary because
ignored build logs are not portable contributor evidence.

# References

- [Input and animation decision](../adr/adr-009-bounded-input-color-animation.md)
- [Contributor validation](../../CONTRIBUTING.md)
- [Hardware workflow](../../README.md)
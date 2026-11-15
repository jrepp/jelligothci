---
title: Gameplay action review and touch latency investigation
author: Codex
created: 2026-11-11T00:00:00Z
tags: [accessibility, gameplay, touch, validation]
id: memo-108
project_id: jelligotchi
doc_uuid: 7a57e7d4-3eda-4c85-8042-17c432e9d8dc
---

# Overview

Recorded gameplay action checks and design findings for a game intended for
children around three to four years old. The game actions below were exercised
through the USB debug tap path and inspected with software-rendered captures.
A physical MENU/CLOSE touch latency capture was completed on 2026-11-12 and
is recorded separately below. No child playtest has been completed.

# Context

The owner reported that Food was not working and that touch response felt
laggy. Memo-117 records the Food command correction and the deployment. This
review continued action checks on that firmware and added temporary timestamps
around the ESP32 touch event, engine input queue, and display present path.

# Gameplay checks

The debug-injected tap path exercises the engine's UI gesture and action logic,
but bypasses the CST9217 touch controller. Screenshots in ignored
`build/esp32/playtest-*.png` files are software renderer output, not physical
panel evidence.

- Food started `EATING` and raised satiety from 0 to 66 after the menu command
  fix. A paused collectible session was retained.
- Water started immediately and reached hydration 1000.
- Basic Care entered `RECOVERING` with a countdown and completed with health
  restored.
- Play started `ENJOYING`; Reading showed a book scene and `READING` status;
  Yoga also started. These actions did not require an extra confirmation.
- Rest started sleep and the More > WAKE action woke the pet.
- Clean started and showed the hygiene meter with soap/bubble feedback.
- Brush Teeth started a staged routine. One debug tap advanced the stage from
  brushing to flossing; the full routine was not completed.
- Activity actions span three pages. `MORE` cycles pages but gives no visible
  page position and repeats the generic More label used for the root menu.
  Some actions are unavailable under current schedule/need rules. A disabled
  Fishing action displayed `CHANGES EACH HOUR`, which does not explain why that
  particular tap did nothing.
- The saved Jelli Chain checkpoint was safely resumed and exited. A Bubble Lab
  detail screen warned `REPLACES OTHER SAVED PLAY`; that replacement control
  was not selected.

# Design feedback

These are observations and recommendations for follow-up, not approved gameplay
defaults. The target age includes children who may not read. Current pixel-text
messages such as `NOT READY`, `CHANGES EACH HOUR`, and the saved-play
replacement warning need an understandable visual cue and immediate feedback
that shows what the child can do next. Activity pagination needs a clear page
position or a simpler choice set. Collection navigation and saved-session
replacement currently demand several steps and expose a consequential choice;
test a shorter route and a safer, visibly reversible way to return to saved
play. Validate hit areas, feedback timing, and disabled-action comprehension
with representative caregivers and children before changing defaults.

# Touch latency investigation

The instrumented firmware records the touch IRQ timestamp, LVGL press/release
event timestamps, time until the engine polls the queued gesture, and time to
the next display present. The capture was flashed to the rediscovered board
`/dev/cu.usbmodem101`; a request for eight short taps on the bottom MENU target
was sent to the owner. At memo creation there were no `Touch latency us` records,
so sensor-to-action latency was unmeasured at that point. The physical capture
below completes that outstanding measurement.

Idle serial samples showed an engine update rate of roughly 35–45 Hz with a
16 ms target interval. Ordinary render samples were around 2–3 ms, while
intermittent samples reported render near 78–81 ms and present near 103–126 ms.
These pauses are a plausible contributor to perceived lag, but the capture did
not include physical touch traces and does not establish causality. The display
adapter performs full-frame buffer comparison and heap integrity checks about
every five seconds (`ports/esp32/main/display_output.c::check_buffers`); their
timing should be isolated and measured before changing or removing diagnostics.

# Validation

## Physical touch capture, 2026-11-12

Rediscovered `/dev/cu.usbmodem101` with `scripts/esp ports`. The old monitor
still held the port but its log had stopped advancing; it was closed and a
fresh serial reader opened with the debug client's existing DTR/RTS handling.
A successful live state request established readiness before requesting taps.
No firmware was rebuilt or flashed for this measurement.

The first sample was mixed touches, not the intended menu control. It recorded
12 events: controller IRQ to LVGL press median 5.398 ms (maximum 9.704 ms),
release to engine median 7.979 ms (maximum 200.595 ms), and release to present
start median 13.311 ms (maximum 207.207 ms). Four events waited over 50 ms for
the engine. Retain this as a separate exploratory sample.

The second sample used the bottom-centre MENU/CLOSE control. It recorded 14
events; the owner confirmed making approximately 14 taps. The pet was asleep,
the game unpaused, and the clock unsynchronized at capture start. The owner
did not report missed taps; an approximate count cannot establish exact event
loss or duplication.

| Measured interval | Minimum | Median | Maximum |
| --- | --- | --- | --- |
| Controller IRQ to LVGL press | 0.440 ms | 6.436 ms | 8.654 ms |
| LVGL press to release (finger hold) | 32.995 ms | 98.841 ms | 113.884 ms |
| Release to engine input poll | 0.486 ms | 7.977 ms | 160.705 ms |
| Release to present start | 71.263 ms | 119.716 ms | 231.344 ms |
| Display-present call duration | 103.188 ms | 104.381 ms | 105.548 ms |
| Release to present return (per-event sum) | 175.019 ms | 225.013 ms | 334.747 ms |

Raw evidence is retained under ignored build output:
`build/esp32/physical-touch-20261112-011315.log` (mixed) and
`build/esp32/physical-touch-20261112-011445.log` (MENU/CLOSE), with initial
state JSON for both and a summary JSON for the controlled sample. Capture
closed after observation and released the serial port.

These observations identify substantial game/render/display latency despite
fast initial touch detection. The log field named `present-copy` times the
whole display-present call, including its locking and diagnostics; it does
not isolate a memory copy. Present return does not establish panel scanout
or finger-to-photon timing. No cause has yet been proven or optimization
verified. Next isolate input queue waiting, menu rendering, display lock,
buffer comparison/diagnostics, and transfer completion, then repeat the same
physical menu test after a measured fix.

`make test` completed with 90 of 109 CTest cases passing and 19 failing.
Failures include food-stock expectations in activity, nutrition, exercise,
game, save, sleep, and decay tests; menu navigation/debug tests also contain
expectations for labels and pages from the previous menu layout. The failure
list is in `build/desktop/Testing/Temporary/LastTestsFailed.log`. These failures
need an explicit pass to separate stale expectations from behavioral
regressions; the gameplay observations above do not replace that work.
`make docs-check`, `make docs-fix`, a dry-run docs validation, and
`git diff --check` passed.

## Latest merged firmware deployment, 2026-11-12

At the owner's request, built and flashed exact merged main
`f17b7dfc130df56132db647d729da0c642a24e84` from an isolated worktree. The
original local ESP-IDF 6.1 and gameplay changes were preserved. This deployment
uses main's pinned ESP-IDF v5.5.5 and app version 0.18.0; it does not include the
local touch instrumentation or menu simplification. The application image was
2,210,224 bytes with 47% of its partition free, and its descriptor and validation
hash passed inspection. Standard USB flashing verified all transfer hashes;
no NVS erase was performed.

The monitor-initiated reset reported `USB_UART_CHIP_RESET`. PSRAM passed its
memory test, CO5300 panel creation and CST9217 IRQ touch registration succeeded,
and the application reached `Pet slice ready`. Audio initialization logged a
channel-disable warning followed by successful codec/mixer startup. No panic
was observed in the monitoring window. Display diagnostics reported matching
buffers and intact guards. These startup observations do not establish physical
touch responsiveness or panel appearance.

A compatibility issue prevents this exact merged firmware from loading the
existing checkpoint: local firmware uses save format 20, while this merged
revision supports 19. Startup explicitly preserved the unsupported checkpoint
and disabled writes. The pre-flash debug state reported nine pets, while the
post-flash state showed the default single Mint pet and unavailable saving.
The old saved roster is therefore not loaded; it was not deliberately erased.
No save reset, conversion, or firmware modification was attempted. Restoring
compatible firmware or implementing reviewed save compatibility remains needed
before progress can persist. The subsequent deployment below resolves this
compatibility limitation. This is distinct from successful flashing/boot.

A USB framebuffer capture showed Mint at home with the MENU button. The owner confirmed the screen
appearance ("looks good"); physical MENU response was not separately confirmed.
Build, image, transfer and boot logs, before/after debug state, and the PNG are
retained under ignored `build/esp32/merged-main-f17b7df/`. The serial monitor and
debug connection were closed after verification.

## Format 20 recovery and case button deployment, 2026-11-12

The owner requested format 20 support and a working case sleep/wake button.
PRs [104](https://github.com/jrepp/jelligothci/pull/104) and
[105](https://github.com/jrepp/jelligothci/pull/105) landed with all current-head
checks green. Exact merged main `85ee0ee` was built and flashed with the pinned
ESP-IDF v5.5.5; application version remains 0.18.0. Transfer hashes passed.

Before compatibility deployment, the NVS partition was backed up locally and
the game-only checkpoint was extracted using the SDK parser. Both blob chunks
passed metadata and data CRC checks; the save checksum and commit marker also
passed. The 4,436-byte format 20 record decoded successfully on the patched
host with nine pets. The raw backup is private local evidence and is not
committed. The firmware supports the existing serialized layout and preserves
unknown-version rejection; it does not incorporate unrelated local gameplay
or SDK changes.

After deployment, the monitor-initiated reset reported `USB_UART_CHIP_RESET`.
PSRAM passed its memory test, panel/touch initialization completed, the PWR
handler reported `PWR input ready on EXIO4`, and the session reported
`save=available`. A subsequent USB state request confirmed nine pets, active
slot 3 / pet ID 5 / form 2, day 5, 20 gifts, and 19 owned collection items.
The framebuffer showed the selected Axolotl at the beach. The clock remains
unknown and needs synchronization; that is separate from checkpoint decoding.
Build, image, flash, boot, state and framebuffer evidence is retained under
ignored `build/esp32/merged-main-85ee0ee/`.

A short PWR press now blanks/restores display brightness, while simulation
continues. This is display sleep, not processor low-power sleep. Touches while
blank and stale queued gestures are suppressed; long holds retain PMIC
behavior. Debounce tests, independent source review, firmware compilation and
hosted checks passed. The SDK permits the additional bounded-timeout I2C
handle, and device startup initialized it successfully. Actual physical
blank/restore and dark-screen touch suppression remain pending owner testing;
an initial capture window recorded no button presses, and a fresh capture was
started after the final deployment. Neither window recorded a PWR toggle before
observation ended, and the serial connection was closed. Serial initialization
alone is not physical button evidence.

# Recommendations

- Use the completed physical capture as a baseline, then repeat it after
  profiling and optimizing the slow game/render/display stages.
- Correlate long frame samples with periodic display integrity checks, then
  profile the display lock and flush completion path.
- Run a supervised child usability session after visual feedback and
  availability cues are adjusted; do not infer accessibility from debug taps.

# References

- [ESP-IDF 6.1 deployment and Food fix](memo-117-esp-idf-6-1-menu-deployment.md)
- [Touch event handling](../../ports/esp32/main/main.c)
- [Display integrity checks](../../ports/esp32/main/display_output.c)
- [Touch input adapter](../../ports/esp32/main/touch_input.c)

---
id: memo-044
title: Device power experiments over the debug port
author: Codex
created: 2026-10-10
tags: [drivers, embedded, experiments, power, sleep]
project_id: jelligotchi
doc_uuid: 02a5a9b0-6b18-4b46-a831-574bfa5a5248
---

Current code organization and factory reuse are described in
[memo-045](memo-045-validation-and-factory-firmware.md). The image hashes and
commands below record the original bench sequence, before profile isolation.

# Scope

Follow-up to [memo-054](memo-054-device-power-and-driver-audit.md). The user
authorized building, flashing and capturing each experiment over the debug port.
Work remains on `investigate/device-power`, based on `15553f6`, in the isolated
`jelligotchi-device-power` worktree. This is an investigation image with explicit
commands, not a completed automatic sleep feature.

Discovered one USB JTAG/serial device, VID:PID 303A:1001, serial
`90:70:69:FE:21:DC`, at `/dev/cu.usbmodem2101`. The port was rediscovered after
USB interruption. No full-chip erase, eFuse changes, PMIC rail writes, charging
changes, or Wi-Fi credential changes were made.

All raw evidence is under ignored `build/device-power-research/` in this worktree.
Startup reports version 0.6.0, ESP-IDF 5.5.5, 8 MB PSRAM, CST9217 ID 9217/project
5734, 466 by 466 resolution, and IRQ mode enabled. Monitor startup deliberately
caused `USB_UART_CHIP_RESET`; that reset was not a panic. The initial live device
had zero transfer failures or framebuffer mismatches. Its wall clock was unknown.

Correction to the first audit: the lockfile and startup identify CO5300 driver
**2.2.0**, not 2.0.3. Other relevant pins remain BSP 3.0.1, adapter 0.6.4,
CST9217 2.0.0, and LVGL 9.3.0.

# Experiments and outcomes

| Finding / hypothesis | Experiment | Observed result | Conclusion and remaining limit |
| --- | --- | --- | --- |
| Touch uses interrupts already | Startup trace and ISR counter attached through the adapter callback | Startup reports IRQ enabled; idle INT read is high | Registration verified. Physical touch edge, hold/release and GPIO wake still need a user tap. A software-injected tap would not test this path. |
| 50 ms reset delay may be too short | Ten reset/checkcode reads each at 50 and 100 ms, with no read retries; finish with a 100 ms reset | All 20 probes passed | No observed reason to change the delay on this module. This does not establish voltage/temperature margins or prove the datasheet's typical time is a minimum. |
| LVGL tick prevents long idle periods | One-second engine hold, then one-second hold with adapter paused | Baseline advanced 1,000 LVGL ticks; paused advanced zero | Existing adapter pause stops the periodic tick. Engine pacing must also change for product sleep. |
| Disabled network worker still wakes | Count returns from its request wait over the same one-second hold | Original: four wakes. Candidate: zero | Candidate waits indefinitely for requests when Wi-Fi is disabled; enabled operation retains the 250 ms timeout. |
| Panel sleep is unused | Pause LVGL, drain queued SPI transfers through a blocking panel command, hold panel asleep one second, restore and force full refresh | Driver calls and restore succeeded; recovery about 1.36 seconds | BSP init table has two 600 ms waits. Full sleep enters deep standby and resets on wake. Timing does not prove visible panel recovery or power saved. |
| A faster display-off path may suffice | Same paused hold using display-off/on rather than deep standby | Restore calls took about 0.25 ms | Candidate for responsive screen blanking, not proof of equivalent current reduction. Requires panel-current comparison. |
| Closing speaker should release audio clocks | PM lock dump before and during codec close | Original BSP: two active I2S APB locks, one remains after speaker close | BSP starts both TX and unused RX. Playback close cannot shut down the unused RX channel. |
| Playback-only transport removes that remaining lock | Reuse pinned ES8311 codec with one host-owned TX channel, same BSP pins and gain | One lock while open, zero active I2S locks while closed; internal free heap increased about 3.5 KiB in the compared image | Candidate avoids unused RX; retains existing codec implementation. Audible quality still needs physical confirmation. |
| MCU light sleep can return safely | Panel sleep, audio close, GPIO11 wake plus requested 100 ms timer, then restore | Timer wake cause 4; operation and restoration succeeded | USB loses requests sent during sleep and may disconnect. Reconnect after the bounded window; do not retry an unknown action. |
| Automatic light sleep works with attached debug USB | Enable PM/tickless idle, then temporarily enable DFS/automatic sleep with panel/audio paused | USB holds NO_LIGHT_SLEEP; automatic sleep count stays zero | Expected connected-USB protection. Unplugged battery operation and current savings remain unverified. |
| CPU power-down is available without more memory planning | Enable automatic sleep with SDK's default CPU power-down setting | SDK logs retention setup failure; retention-capable largest free block is zero | Final candidate retains CPU state during light sleep. This avoids the failed allocation; it is not the lowest possible power mode. |
| A long gap can use the normal game frame path | Run a three-second gap against an isolated copy of current game state | Live path discards 1,000 ms with empty initial backlog; bounded resume advances 30 ticks | Product sleep needs explicit time recovery. Test does not change the live pet or validate overnight journal semantics. |
| PMIC rails/RTC may offer further savings/wake sources | Read-only AXP2101 registers and existing clock status | Registers 03/26/80/90 read 4a/08/0f/ff; RTC remains unknown | Readback is not rail current or alarm-wake evidence. Preserve shared touch/MCU supply and RTC trust rules. |

One-second timings with PM profiling include lock-dump overhead. Display and
heap checks are five-second samples, not instantaneous snapshots at every
response. Post-trial captures show the engine framebuffer, not the physical
panel. Raw PA GPIO input reads do not establish output voltage when input is
disabled on that pin. None of these measurements establish battery current.

# Retained changes

- `audio_board.c` owns a TX-only I2S transport and the existing codec interfaces.
  It uses BSP pin constants and copies the BSP's DAC/clock/gain settings.
  Failure paths free the partial startup allocation; successful resources live
  for the application lifetime. Do not also initialize the BSP duplex audio path.
- Sound close/reopen is serialized with the audio worker using a static mutex.
  This is invoked explicitly by diagnostics; ordinary playback stays open.
- Disabled networking waits on its command queue instead of a 250 ms timeout.
- PM profiling and tickless idle are enabled, but DFS and automatic sleep remain
  disabled except during the explicit `auto` trial. Connected USB retains its
  SDK sleep lock. CPU power-down is disabled for this experiment profile.
- `power` commands and the capture runner provide repeatable, bounded trials.
  Diagnostics freeze pet progression for their duration and restore brightness,
  panel and audio state. The touch trial consumes a release gesture; when its
  timer wins without touch, the next physical gesture is consumed.

Existing engine/canvas ownership and DMA strip lifetime are unchanged. The
panel handle is recorded by the draw callback and read after pausing LVGL.
A blocking SPI parameter write drains previously queued color transactions
before sleep. Adapter resume is followed by explicit full-canvas invalidation.

The isolated timing probe uses 4,072 bytes of static game scratch, plus a
512-byte report buffer. There is no extra framebuffer, task, or frame-loop
allocation. Diagnostic IRQ and worker counters use SDK critical sections.
I2C probe handles and codec/transport interfaces are bounded startup allocations.
PM support and profiling also have SDK memory costs; observed stack/heap
values should guide any production power manager rather than assuming all free
heap is suitable for retention or DMA.

# Failures and remedies

The first compile rejected a lock-free C atomic assumption on the ESP32 target.
Replaced it with the SDK's task/ISR critical sections. Source complexity checks
rejected the first combined trial function; split hold and restore duties.
A codec constructor takes a mutable config pointer; corrected the local config
type without weakening compiler checks.

The first explicit light-sleep capture sent its next request while USB was
asleep and timed out. Another read observed the temporary disconnect as
`Device not configured`. The board returned successfully in both cases;
querying `power result` after reopening recovered the outcome. The runner now
waits for the bounded window and reopens the port by its serial identity. It
never automatically repeats an action. This behavior is documented in the
[hardware guide](../../docs/hardware.md#power-experiments-over-usb).

The original BSP path also logged attempts to disable an already-disabled I2S
channel during reopen. The important power finding was the independently active
RX channel, established by PM locks and source inspection. Candidate audio
validation must include actual cue submission and post-close/reopen logs.

CPU retention setup failure is distinct from USB's deliberate sleep lock.
Keeping CPU state during light sleep removes that optional retention allocation;
do not present this as battery-current validation or silently bypass USB locks.

# Reproduction and evidence

Use the current discovery result, close other serial clients, and follow
[the power experiment commands](../../docs/hardware.md#power-experiments-over-usb).
A new SDK was bootstrapped in this worktree. Fresh PM configuration used:

```sh
./scripts/esp idf -DSDKCONFIG=/absolute/worktree/build/device-power-research/sdkconfig-pm build
```

For an existing generated config, tracked default changes do not replace all
old values. The experiment explicitly updated the generated CPU power-down
selection after retaining the earlier config/evidence. Final config and image
are saved with the raw evidence. Build and flash remained separate operations.

Evidence groups:

- `baseline-state.json`, `baseline-display.json`, `baseline.png`: original device.
- `startup-01.log`, `image-01.txt`, `flash-01.log`: first diagnostic deployment.
- `trials-01/`: baseline holds, blanking, pause, panel and audio trials.
- `trials-light-01/`, `trials-light-02/`, recovered JSON files: USB failure and recovery evidence.
- `trials-light-03/`: successful identity-based reconnect capture.
- `trials-02/`: 20 touch resets, isolated timer/network observations, panel and light sleep.
- `trials-pm/`: original duplex audio lock observations and connected-USB automatic sleep.
- `trials-candidate/`: TX-only audio, disabled-network wait and display-off comparison.

# Validation and remaining work

Initial diagnostic image: `make test` passed all 62 tests, including headless SDL
and debug CLI integration. ESP32 builds passed strict diagnostics after the
fixes above. C formatting, Cppcheck, and size checks cover the added port code;
portable-core clang-tidy passed during the initial lint run. Its size failure
was corrected and the size gate rerun. No portable game logic was changed.

Final image SHA256 (sixth flash):

```text
492d22febeb81b43b70fb5665534d49fba14621efe7b5a8ed413d9efe1892292
```

`image-06-final.bin`, `image-06-final.sha256`, `sdkconfig-final`, and the final
source patch retain the tested image/configuration/source evidence. Flash logs
report verified transfer hashes. `trials-final/` covers all ten non-touch modes
on the preceding image; the final source adds a display mutex around brightness
commands. `trials-touch/` covers the final ten-second GPIO/timer window, and
`trials-verified/` repeats blanking, off/on, audio, and automatic PM on that image.

The ten-second window woke by **timer**, cause 4, with zero touch IRQs. It held
for 10,003,191 microseconds and restored in 1,384,458 microseconds. This verifies
the fallback and recovery, not physical touch wake. LVGL tick advancement and
network worker wakes stayed at zero during that hold. No physical tap was
observed. USB reconnection recovered the result and a framebuffer capture.

The final PM configuration no longer logs the CPU retention setup failure.
Connected USB still prevents automatic light sleep as intended. Final sampled
main-task stack reserve reached 768 bytes during profiling and 720 bytes in
the post-restart display check; monitor this margin
before adding more diagnostics. Framebuffer checks, guard checks and heap
integrity remained clean, with zero transfer failures. Two tap cues submitted
1,433 samples each before and after audio close/reopen. The SDK still emits a
nonfatal already-disabled I2S-channel warning during reopen; successful return
and cue submission do not prove audible quality. Do not hide that warning or
claim the whole audio lifecycle is validated.

Final C formatting, Cppcheck, size checks, and Python compilation passed.
Documentation validation passed with no findings; the repair pass made no
changes. After the sixth image was restarted, `startup-06-final.log` confirmed
debug readiness and clean display, guard and heap checks. `final-power.json`
and `final-display.json` retain the post-restart diagnostic state. The restart
also cleared the unconsumed touch-trial gesture filter.

Physical display, touch wake, audible quality, unplugged automatic sleep, and
current measurements are still separate checks. CST9217 monitoring/sleep command
protocol is still missing, so no guessed touch-mode writes were made. RTC alarm
routing through the expander, PMIC rail sequencing, and QMI8658/ES7210 unused
peripheral states remain follow-up experiments requiring their exact protocols.
Deep sleep is deferred until trusted time and overnight save/journal recovery
have dedicated tests; it is not a drop-in replacement for this light-sleep trial.

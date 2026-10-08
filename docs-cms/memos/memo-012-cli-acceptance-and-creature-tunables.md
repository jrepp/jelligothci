---
title: CLI acceptance and creature tunables
author: Codex
created: 2026-10-08T03:00:09Z
tags: [debug, deployment, memo, tunables, validation]
id: memo-012
project_id: jelligotchi
doc_uuid: fd9d6ac5-66d6-4c43-a2f1-8f16d8081073
---

# Initial Deployment and External Acceptance

The user requested flashing the ring/art/particle pass, then testing the CLI
externally against SDL first and the live ESP32 second. Fresh discovery identified
USB Serial/JTAG at `/dev/cu.usbmodem1101`, serial/MAC 90:70:69:FE:21:DC. The first
image was 688,080 bytes, version 0.1.1, file SHA-256
`4ce9c3fabc48e3eb6578408c6bf3d31f366fc9a053fbc6c72a6e38047c726e57`.
Transfer hashes passed. Monitor startup matched ELF prefix `6f72dd10d`, passed
8 MB PSRAM testing, initialized CO5300/CST9217/LVGL, and reached pet/debug ready.
The monitor-triggered USB_UART_CHIP_RESET was expected; no panic was observed.
Engine samples reported 41–42 Hz, including a 43 ms render/101 ms present sample;
these are not panel FPS or particle-specific measurements. Logs use the
`build/flash-rings-*` prefix. The monitor was closed before CLI access.

The manual harness [acceptance.py](../../tools/debug/acceptance.py) invokes the
public shell CLI in separate subprocesses. It does not import the client or use
mock transports. It verifies JSON state, visible controls, label/numeric/coordinate
inputs, invalid-input rejection, BACK/CLOSE, sleep/wake, actual PNG dimensions,
chunk checksums and nonblank pixels, matching capture state, release, and reconnect.
It closes the menu and restores initial sleep state after a successful run.
Gameplay time continues naturally, so this is an interactive acceptance test,
not a byte-identical world-state restoration tool.

Local acceptance passed 24 invocations. The initial USB run failed immediately:
`state` returned rendered=false. A raw diagnostic showed a fresh USB_UART_CHIP_RESET
on each CLI open. Pre-opening with DTR/RTS false was causing resets on this macOS
native USB Serial/JTAG path. Keeping both asserted preserved a progressing world
clock across reconnects. This is now the CLI default; no input retry masks resets.
The corrected USB test passed all 24 invocations. Screenshots took approximately
8.69 seconds locally and 8.58 seconds over USB, within the 30-second capture lease.
Evidence is in `build/acceptance-local/`, `build/acceptance-esp32/` (original failure),
and `build/acceptance-esp32-fixed/`. Other OS/USB-UART adapters remain unverified.

# Stat and Animation Fixes

The user observed occasional wraparound while touching stats and excessive speed.
Manual stat selection previously retained the automatic carousel's slide phase;
it now settles on the next displayed stat and restarts its hold interval. Repeated
inputs within a frame advance predictably. The 18-second master-clock modulo was
removed in favor of a 64-bit elapsed clock; pause stops that clock. Timing changes,
creature/form changes, and rollover reset animation anchors deliberately.

Idle frames now default to 900 ms, twice their former duration. UI durations scale
to 300 percent: default stat hold 9.9 seconds, slide 900 ms, particle step 60 ms.
Simulation needs, activity effects, cooldowns, and offline progress retain their
existing timing. Visual slowing must not silently change the care economy.

# Pluggable Tunables

The portable registry defines six named, bounded parameters with units/defaults:
idle frame duration, general UI duration scale, stat hold, stat slide, burst count,
and particle spread. Every active renderer/particle consumer resolves the active
creature's stable ID and form through the registry. There are no string lookups
inside drawing primitives. Adding a parameter requires an enum/definition and a
consumer; debug listing/range enforcement uses the same definitions.

Resolution is default, immutable registered form profile, global runtime override,
then stable creature-ID override. Registration validates profiles atomically,
rejects duplicate forms, and borrows caller-owned immutable storage. Up to 16 form
profiles and eight creature overrides are bounded. Empty override slots are reusable.
Both current forms inherit the same defaults until content supplies a profile.

Runtime overrides are session-only and are not encoded into saves. Gameplay-rate
and reward tuning needs a future content/save-version decision; this pass exposes
presentation tuning only. Host sizes are 304 bytes for tunables, 216 for particles,
672 for UI, and 1,840 for the pet engine. Compile-time bounds cover both targets.
No heap allocation or additional framebuffer was added.

The debug extension uses `tunables ID` and `tune ID NAME VALUE|reset`, with ID 0
for global scope. Unknown pet IDs and out-of-range values are rejected without
mutation. Capture/resume block writes. Replies retain the bounded 2,048-byte buffer.
The CLI rejects malformed names/numbers before forming wire commands.

# Validation and Follow-Up

Local expanded acceptance passed 39 separate CLI processes, including overrides,
inheritance, rejection, reset, and stat-tap settling. Shared tests cover profile
precedence, invalid mutation, capacity/reuse, timing, and mid-slide taps. Final
firmware acceptance and build results are appended after deployment below.

Physical readability, touch feel, battery impact, and subjective animation speed
still require user observation. The serial acceptance verifies commands and the
native framebuffer, not emitted panel pixels or the touch sensor path.

# Final Tuning Deployment and Results

The tuning build was then built and flashed to the freshly rediscovered same
ESP32-S3. App version remains 0.1.1; the binary is 690,144 bytes (0xa87e0), with
34 percent of the 1 MiB app partition free. Application SHA-256:

```text
82c4164680a3fa67638d1f27aec570e84681205281e8c72f7b1bddcea3672444
```

ELF descriptor SHA-256:

```text
e8226a3bdb6ed46007ad8932a331f89145123b99fca731734d22a27344e48d27
```

All flash transfer hashes verified. Startup matched the ELF prefix, passed PSRAM,
initialized panel/touch/LVGL, and reached pet/debug ready without a panic. The
monitor caused the expected USB_UART_CHIP_RESET and was closed before testing.
Three engine samples reported 51–53 Hz; a sampled redraw was 43 ms plus 100 ms
presentation. Lower animation frequency reduces redraw frequency, but these
samples do not establish improved panel FPS or worst-case particle latency.

Final checks passed:

- 16 desktop tests, seven SDL-free tests, and 16 sanitizer tests.
- Clang-Tidy, Cppcheck, formatting, 56-file size gates, and all repository hooks.
- Asset validation and browser verification of the slower preview and mid-slide tap.
- Local acceptance followed by USB acceptance: 39 external CLI invocations each,
  including tunable defaults, global/per-creature precedence, invalid values,
  invalid scope, reset inheritance, stat settling, and post-capture inputs.
- Actual USB framebuffer PNG inspected; screenshot PNG and accompanying state agree
  on the Moments ring. Physical screen/touch feel remains a user observation.

Detailed evidence: `build/acceptance-tunables-local/report.json`,
`build/acceptance-tunables-esp32/report.json`, their `moments.png`/JSON captures,
and `build/tunables-{flash,startup,image,final-tests,hooks}.log`. Both acceptance
runs restored prior idle tuning and sleep state and left the menu closed.

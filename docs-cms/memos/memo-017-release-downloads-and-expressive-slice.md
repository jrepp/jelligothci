---
id: memo-017
title: Native release downloads and expressive slice
author: Codex
created: 2026-10-08
tags: [assets, debug, releases, rendering]
project_id: jelligotchi
doc_uuid: 46bf7871-e230-4542-ac55-d62cd4a4d6f2
---

# Scope

The user requested a tagged development release with a downloadable native game,
editable artwork, and preview page, followed by a device flash. The reusable
[release workflow](../../.github/workflows/release.yml) now packages macOS builds
on macos-15 after core, sanitizer, and quality checks. Existing semantic release
versioning remains Release Please-managed; development tags use `dev-`.

# Packages

[The packaging script](../../scripts/package-release.py) emits a macOS game ZIP,
source-art ZIP, standalone preview HTML, preview ZIP, and SHA256SUMS. CI adds a
Git source archive and regenerates checksums covering all downloads. The game
bundles SDL and, for SDL2-compat, its dynamically loaded SDL3 library and license
notices. Runtime dependencies are rewritten relative to the bundled loader;
unknown dependencies fail packaging. The package records its architecture and
minimum macOS version from Mach-O load commands. It is ad-hoc signed and verified,
not Apple notarized. Opening the app starts an unsaved session; the embedded CLI
supports `--save` as documented. The Windows x64 job builds pinned SDL2 2.32.10 with the static MSVC runtime,
bundles its DLL/license, and smoke-tests the extracted ZIP. Windows live debug
sockets remain unsupported; portable protocol tests still run. No Linux ZIP is
promised. Windows execution awaits the first GitHub workflow run.

The initial shell-based app entry point failed nested-code signing. Using the
actual Mach-O game as the bundle executable fixed signing; package creation also
runs a headless smoke test. Archives must be tested again after extraction.
Publication only targets an existing tag/release at the checked-out commit.

# Visual and debug changes

There are 70 PNGs: sixteen creature frames, eight celebration sprites, and the
existing icon, prop, font, and background sets. New curious/content keys add just
two paintover bases per form, derived from the existing body artwork. Idle timing
uses uneven holds and occasionally skips gestures, entirely from injected time.
Sleep, activity, illness, and touch reactions retain priority over idle poses.
Music, ideas, and rainbow effects share the existing 24-slot, eight-byte particle
pool. Stars and gift ribbon iconography have simpler, bolder shapes. Garden now
uses a flower icon; scenery has clearer window/floor and bush/grass silhouettes.
Status captions have a bounded, soft RGB565 dark scrim. Bottom control icons use
cached opaque centroids to align with the visible glyph height.

Raw art is 127904 bytes, up 10336 bytes. With the existing 12288-byte metadata and
definition allowance, the proposed pack is 140192 bytes under a 147456-byte
(144 KiB) ceiling. The larger art allowance does not enlarge the particle pool or
add a runtime decode buffer. Firmware image measurements remain separate.

Periodic quiet coos run after 30 seconds of idle awake time, suppressed by menus,
sleep, activity, or recent input. `audio.coo_enabled` and `audio.coo_interval_ms`
are namespaced tunables. The event viewer has explicit categories, grouped events,
navigation hidden by default, pause/follow, and a compact live-needs display.
Its cheat panel changes active-pet needs/bond or item counts and can heal the pet.
Cheats are range-checked, logged as Debug events, blocked during capture/resume,
and rolled back when they violate recovery or item-reservation invariants.

# Validation and remaining work

Validation logs are retained under ignored `build/release-*`; deployment evidence
will be added after the final checks and authorized flash. Physical screen,
touch, and speaker quality require observation independently of serial startup.

Long-term mood/preference learning is still future work: separate transient
reactions from slowly learned, bounded affinities; cap repeated interaction gains
per day, include time-of-day context, and gradually decay unused associations.
Persist learned values with an explicit save-version migration. Validate with
long deterministic simulations, timestep partition equivalence, and repeated-tap
adversarial tests before enabling learning. Existing fixed preferences are not
learning. Further silhouette/face paintovers, context-specific reward particle
selection, and grouping passive stat decay separately from action deltas remain
polish tasks. No claim of completed learning or physical verification is made.
The later requested UI pass moves pet selection and sleep/wake exclusively under
Settings, adds a large clock, removes visible back/close labels, and animates ring
icons outside the panel before entering the next ring. Hit targets are disabled
while moving and the CLI waits for a settled ring. The debug pipe fixture advances
injected time only outside capture; advancing on every pixel request had correctly
triggered the capture hard timeout and was fixed.

Shots now retain 1–3 tap progress per pet and start a one-hour cooldown on the
first successful tap. The remaining taps of that round may be completed; after
completion the option is hidden until eligible again. Medicine is one small dose
with a one-hour cooldown and a smaller need boost. These histories are encoded
in checkpoint version 2; version 1 defaults them to never administered. Device
persistence and external pet-memory files remain the next storage increment.

# Local validation before deployment

`make test` passed 21/21; `make core-test` passed 9/9; ASan/UBSan passed 21/21.
`make lint-c` passed formatting, Clang-Tidy, Cppcheck, and the 77-file size gate.
The ring timing and render-key comparisons were split into small helpers to
retain the existing complexity limit. `scripts/release-validate`, actionlint,
workflow-pin checks, docs-check, and docs-fix passed. The incremental version test
fixture now includes the new audio exporter sources.

The external local acceptance test passed 174 independent CLI invocations,
including rendering/capture, transitions, health routines, and namespaced tunables.
The console cheat endpoint successfully changed energy and displayed a Debug event.
Native home and settings screenshots were inspected. The macOS ZIP was extracted
outside the repository and passed headless launch, deep signature verification,
non-system dependency inspection, license presence, ZIP integrity, and SHA-256
checks. Windows CI execution and hardware checks are separate pending evidence.

Final readability follow-up: sleep Zs now consistently render at 2× with a dark
two-pixel shadow and brighter medium-blue fill. Their bounds remain within the
existing 16-pixel particle radius. The wake/sun icon now has four short symmetrical
rays instead of eight long strokes. No particle-pool or art-payload growth.

# Device deployment

Flashed source commit `8f44634` to the rediscovered native USB device at
`/dev/cu.usbmodem1101` (VID:PID 303A:1001). Transfer hashes verified. Application
size is 813760 bytes, leaving 234816 bytes in the 1 MiB app partition. Binary
SHA-256: `fe0afdb10943d59ffeb9958cb5169dee730ecaa10aef111b588351d70a1d70b4`.
The monitor-induced USB reset reported app 0.1.1, ESP-IDF 5.5.5, PSRAM memory test
OK, CO5300 display and CST9217 touch ready, LVGL running, ES8311 mono PCM16
22050 Hz ready, and debug ready. Audio startup heap delta remained 6788 bytes
with 254311 bytes free. One changed frame measured render 108 ms / present 100 ms;
ordinary update samples were 50–57 Hz, not panel FPS. Physical screen/touch and
speaker audibility remain user-observation checks. The device viewer reconnected
on loopback port 8766 and successfully polled state.

The first GitHub release validation passed core builds on Linux/macOS/Windows,
but the Linux SDL build caught an integer-promotion warning in screenshot hex
encoding. Casting RGB565 to uint32 before shifting resolves the GCC sign warning
without weakening diagnostics. This follow-up is separate from the flashed
gameplay/visual changes. Windows packaging awaits a successful release workflow.

# Swipe and clock follow-up

Added a shared, allocation-free press/release recognizer to SDL and ESP32. It
uses three scalar fields (12 bytes on these hosts) per host, lives on the input
thread/LVGL task, and sends the existing bounded queue one tap or swipe on
release. Canvas scrolling is disabled so LVGL does not claim game swipes.
Invalid endpoints, ambiguous diagonals, and intermediate drags are dropped.
Up opens; down backs out one level; horizontal swipes select six fixed stat
pages without automatic movement. The debug protocol and CLI expose the same
four directions. A cancelled press cannot produce a later tap.

The gear has six broad teeth, a small hub, and one highlight. The settings clock
is smaller and its own gear opens six explicit timezone/hour/minute controls.
Sleep has a visible ON/OFF badge. Clock adjustments are session offsets from
host-local or simulated time, not a hardware RTC write or saved device setting.
Timezone increments are 30 minutes with −12/+14 hour bounds; hour/minute edits
wrap at midnight. The clock edit view has its own Back step and debug fields.
This is a scoped navigation improvement; collectible/gift and contextual food
flows in RFC-002 remain future work.

Menu motion has a brief ease-in followed by a quartic ease-out: roughly 75% of
travel occurs in the first 30% of each phase; the remaining time settles onto
integer pixels. Both entry and exit use that timing, preserving current-position
retargeting. No new framebuffer, dynamic allocation, or particle pool was added.

Small Sol-agent review fixes: active eating/play/gift poses take priority over
lingering touch reactions; intentional sound-note pauses now output zero PCM.
Review also caught oversized clock-gear hit testing, same-page transition reversal,
and menus freezing while paused. Bounds-based hit tests, current-position
retargeting, and immediate paused navigation now cover those cases. Dental
routines finish early if their care benefits are already full, avoiding a stuck
half-completed routine.

Action availability uses the actual command implementation on a fixed caller-owned
scratch game, with events detached; it never mutates the live pet or emits preview
events. Disabled buttons are dimmed and exposed as disabled in the debug console.
Activity-start buttons have play badges, and successful timed actions return to
the pet scene. Settings displays the build's VERSION-derived release number in
large text; this number does not imply that uncommitted changes were released.
The small green clock label and save-status caption were removed/replaced.

The preflight workspace is capped at 1280 bytes by a compile-time assertion
(1272 bytes measured on macOS); total desktop UI state is 2384 bytes and engine
state is 5816 bytes. It is engine-owned, not an ESP task-stack temporary. The
existing framebuffer count and 127904-byte raw artwork payload are unchanged.

Follow-up validation: desktop 23/23, portable core 10/10, ASan/UBSan 23/23,
C analysis/format/size checks (83 files), artwork validation, docs check/fix,
and ESP32 compilation passed. External desktop CLI verified swipes, fixed stat
selection, clock edits, timed activity return, and disabled button state;
Settings/clock/activity/disabled snapshots were visually inspected. UI tests
cover same-position transition reversal, paused navigation, timezone bounds,
midnight wrap, and input bounds. Actual physical touch feel remains unverified.

Flashed this follow-up to rediscovered USB VID:PID 303A:1001, verified transfer
hashes and startup. Image is 815408 bytes (233168 bytes free in the app partition),
SHA-256 `32a92a7a438d8fc4e1ec3f2f3dbadf6bd0903872f5e630690a4570ac237d1e91`.
Startup reported app 0.1.1 / IDF 5.5.5, ELF prefix `b1adaa3a9`, PSRAM memory test
OK, display and CST9217 touch ready, audio mono PCM16 22050 Hz, and debug ready.
The LVGL built-in gesture warning is expected: this implementation uses ordinary
press/release pointer events with its own shared recognizer. Sound heap delta
is 6788 bytes, free internal heap 253031. One changed frame sampled render 110 ms
and present 100 ms; ordinary engine updates 50–57 Hz, not panel FPS.
USB debug reached Settings and Clock, then returned to the closed main scene.
An immediate horizontal command during the closing transition was ignored by
the transition guard; settled horizontal navigation is desktop-tested. The
viewer reconnected on loopback 8766. Physical display/touch/audio remain separate
user checks.

Release workflow follow-up: Linux validation, all core platforms, and Windows
packaging passed run 37734291508, but macOS `debug_local` exceeded its 40-second
limit before publication. The exact stalled stage is not yet established;
466 sequential screenshot requests and paced request/reply frames are a latency
candidate. No tagged download release has been published for this slice yet.

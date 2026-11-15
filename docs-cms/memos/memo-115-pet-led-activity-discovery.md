---
title: Pet-led activity discovery implementation and validation
author: Codex
created: 2026-11-13T18:48:10Z
tags: [activities, gameplay, input, persistence, validation]
id: memo-115
project_id: jelligotchi
doc_uuid: b4abf46a-66e5-4d4d-a54a-8096522ae1ef
---

# Scope and implementation status

The owner requested a complete implementation and validation of
[RFC-013](../rfcs/rfc-013-pet-led-activity-discovery.md). Work is in progress in a
checkout that already contains food, menus, firmware SDK and touch-instrumentation
changes. Those edits are preserved. This memo distinguishes implemented behavior
from validation still outstanding; it is not a completion or deployment claim.

The ordinary scene now opens the root wheel through a pet tap, without comfort
or wake effects. A continuous three-second hold opens Settings with progress
feedback and consumes release. Scene targets retain priority; active routines
retain their own input, including their existing explicit wake interaction.
The bottom navigation control is drawn only while a menu is open. Enter and
swipe navigation remain alternative routes.

[Pointer handling](../../core/pet_pointer.c) freezes the pet target at press,
uses injected monotonic time, and cancels on movement, focus loss, scene/pet
changes or cancellation. SDL reports pointer edges; ESP32 sends them through
its existing bounded queue. Queue overflow discards the incomplete gesture and
queues cancellation. No new task, framebuffer or steady-state allocation is added.

[Invitation logic](../../core/invitations.c) generates bounded per-pet offers,
with [authored leisure policies](../../content/activities.json) and a shared
[content validator](../../cmake/JelliActivities.cmake). A leisure offer can relax
its random-choice and authored window gates while preserving form, prerequisite,
cost and reward rules. The command path validates the actual persisted offer;
there is no caller-controlled bypass flag. A successful start consumes the offer;
a failed command or preflight leaves it pending.

The ledger holds timed offer bitsets and one independently expiring leisure
offer. Generation interval and authored lifetimes are initial implementation
tuning, not separately playtested defaults. Pending offers are never replaced
under pressure: a live leisure offer occupies that slot until accepted or retired.
A daily announced bitset prevents repeated invitations for the same activity;
ordinary replay still follows existing activity rules and reward accounting.

# Persistence and time policy

[Save format](../../include/jelli/save.h) and
[explicit invitation codec](../../core/invitation_save.c) persist pending, seen,
announced, selection, expiry, clock basis, high-water time and next generation.
Old saves load with an empty ledger. The pet containing the ledger supplies its
stable identity; saved IDs refer to append-only activity content. Known-clock
offers are reconciled before display, including after load/resume and display or
pet wake. A temporarily busy/asleep pet or insufficient resources suppresses
readiness without retiring a still-valid offer.

Wall time is preferred; without it, an explicit game clock or simulated pet time
supplies a separate time basis. Unknown-time simulation can generate leisure
offers with relaxed windows but does not advertise unverified timed offers.
Changing clock basis retires old deadlines and delays the next leisure selection
by the generation interval. Backward time suspends readiness and generation until
the previous high-water mark is reached. Forward time expires stale offers without
replaying missed intervals or extending deadlines. The daily epoch is derived
from the saved high-water clock rather than serialized redundantly.

# Presentation

[Discovery projection](../../core/pet_discovery.c) uses full command preflight
for actionable cues. The thought bubble cycles through unseen activity icons;
the root Activities item pulses briefly and retains a marker. Opening Activities
snapshots a ready-first ordering for that visit. Expired choices keep their target
position and explain expiry on refusal. Seen state changes only after the relevant
page has been rendered; off-page offers retain their cues. Rendering itself does
not generate offers, acknowledge them or change gameplay.

# Resource accounting

The first full-roster encode exceeded the existing save buffer by four bytes.
Removing the redundant per-pet day field restores the existing storage bound;
no partition or save-buffer expansion is needed. The codec adds 38 bytes per pet.
The in-memory ledger is 40 bytes per pet on the development host; the full
16-pet save encodes to 8,068 bytes within the unchanged 8,192-byte capacity. Game preflight
and platform save workspaces have explicit revised bounds in their owning source;
the shared pet-engine bound remains unchanged. The development host measures `JelliGame` at 8,720 bytes. The normal application image measures 2,246,528 bytes. ESP32 object symbols
measure engine storage at 30,256 bytes, session storage at 25,736 bytes and board
storage at 352 bytes, including the preserved case-button state. These are
static-object sizes, not peak stack or total system heap measurements.

# Validation evidence and remaining work

Focused tests pass for invitation access beyond the ordinary random gate, pure
preflight, cost refusal, acceptance, persistence, acknowledgement, hourly changes,
expiry boundaries, clock rollback, old-format migration and malformed clock basis.
Pet gesture tests pass below and at the hold threshold, release consumption,
drag-out-and-back cancellation, cancellation events and asleep-pet navigation.
Discovery tests cover ready-first ordering, rendered acknowledgement and stable
targets during expiry. The complete roster encodes within the original buffer.

The first firmware build encountered the old save-workspace assertion. The
ledger's two copies in the ESP save workspace require a larger explicit RAM
budget; updating that bound allowed the firmware build to pass. The subsequent device flash and startup evidence are recorded below; physical
touch/display validation still awaits owner feedback.

A software-rendered thought-bubble frame was inspected: the pet, activity icon,
need tile and navigation hints are visible on the round surface. Wheel, activity-page and hold-progress captures were subsequently inspected.
Physical interaction remains a separate owner check. Capture artifacts are ignored build output.

The initial broad test run observed stale food-stock and menu expectations in
this dirty checkout. Its later content/preview tests overlapped implementation
edits, so that run is not a clean baseline for every failure. The earlier complete sanitizer run passed 111 tests, and the core run passed
48 tests. The final rerun includes the brought-forward case-button test; its
results are recorded separately below. Static analysis passes clang-format,
clang-tidy, Cppcheck and all source-size/complexity limits.

The default formatter scans tracked paths, including pre-existing deleted food
files, and failed on those missing paths. Formatting was rerun over existing C
sources. Static checks use a temporary Git index containing the current tree so
new files and existing deletions are represented without changing the user's
real staging area. The final static checks pass. `git diff --check` reports no whitespace errors.


# Deployment preparation

Port discovery found the attached ESP32-S3 USB device after the owner connected
it. Before changing firmware, a private NVS backup was captured under ignored
`build/` output, and the debug state confirmed a nine-pet roster with the active
pet asleep. No secrets or NVS contents are included here.

The workspace predates the deployed v0.19.0 case-button support. The existing
case-button driver, debounce state machine, display-brightness helper and host
test were brought forward from `origin/main` at `9c796d6`, then connected to the
new pointer cancellation path. This preserves short-press display blank/restore;
it does not add a new power policy. The working checkout's pre-existing ESP-IDF
6.1 and touch-instrumentation edits remain intact. The image descriptor retains
the checkout's 0.18.0 VERSION; this is a development test image, not a release or
an assertion that its source equals the deployed release.

The SDK's esptool v5.6 rejected the older `image_info --version 2` spelling;
`image-info` successfully inspected the application descriptor and validation
hash. This is a tool-interface change, not an image failure.

Software frame review covers the thought bubble, marked main-wheel Activities
item, ready-first activity entries and hold progress. The activity page count
was moved below the pet so it does not obscure the sprite. These captures do not
establish physical touch or display behavior.


# Device startup and checkpoint verification

The normal test image was flashed to the discovered board. Esptool's fast
reflash failed its final content verification and automatically retried with the
full application image; that retry verified successfully and the command exited
successfully. The cause of the fast-reflash mismatch is not established. No
full-chip erase, NVS restore or eFuse change was performed. The prior NVS backup
remains available privately under ignored build output.

The serial monitor reported `USB_UART_CHIP_RESET`, matching the monitor/reset
operation, and the development image version described above. Its reported ELF
hash prefix `5c85c5f59` matches the built image descriptor. PSRAM passed its memory
test; panel, touch, case PWR input, audio, pet session and debug initialization
completed. Buffer diagnostics reported equality and intact guards with no
mismatches. The brief observation recorded engine update rates of 59–65 Hz;
these are not physical panel FPS measurements or a performance benchmark.

Existing panel-format and disabled-gesture warnings remain. The I2S
not-enabled-yet diagnostic was followed by successful audio initialization.
No panic was observed during the captured startup interval. The monitor was
closed before the debug client connected.

Debug state before/after confirms the nine-pet roster, active pet identity and
slot, form, asleep state and owned collection mask survived. Save status is
available, and the new closed-scene `PET` target is exposed instead of `MENU`.
This verifies checkpoint loading and new firmware execution, not physical taps.
The owner has been asked to check sleeping-pet navigation, the three-second hold
and release, bottom Back, the invitation flow and case-button blank/restore.

# Final software checks

The desktop suite passes 112/112 tests and the core suite passes 49/49 tests.
That sanitizer run was superseded by the revised validation below. Normal and power-validation firmware
builds pass, and normal, power and factory profile-isolation checks pass. The
factory image also built successfully during this work. Documentation validation
passes for 125 documents; `docs-fix` reports no unresolved findings.


# Owner feedback and revised wheel

The owner confirmed that the menu and Back worked well on the first test image,
then reported too many disabled activities and an unclear Play/Activities/More
split. They explicitly selected two ready shortcuts plus Activities on the left,
with Food, Water and Care fixed on the right. They clarified that Care must open
health/hygiene choices, not invoke the non-interactive Basic Care treatment.

The revised implementation follows that layout. Both left shortcuts and the
Activities view include only actions passing shared preflight when opened;
invitations rank first. Ordinary Play and Exercise join the same Activities view.
Targets remain stable during a visit and eligibility is rechecked on activation.
Care opens the routine wheel without modifying needs, bond or health. Yoga is
an activity; Rest/Wake replaces its old care-wheel position. The retired Basic
Care UI action cannot issue a treatment command. Existing recovery logic remains
shared by individual health routines. Settings → Pet Menu retains Pets and Gifts;
swiping down from the closed scene provides keyboard/debug access to Settings.

The initial final sanitizer run had one timeout in `debug_clock` after 111 other
tests passed. Its nine real sleep/wake cycles repeated decorative menu transitions
and ran near the existing deadline on desktop too. A session-only animation
tunable now shortens those transitions after default-speed navigation is checked,
while preserving all nine cycles and journal assertions. Targeted sanitizer and
desktop reruns passed in approximately six seconds; the revised menu paths reduce
that further. No production timeout or animation default was changed.

The revised layout passes all 112 desktop tests and 49 core tests, plus focused
post-refactoring navigation tests. Static checks pass, including the unchanged
function/file/complexity limits. Normal and power images build and pass profile
isolation. The manual external CLI acceptance run passes 61 separate invocations
covering current labels, navigation, input rejection, PNG/state capture, reconnect,
sleep/wake and Back/Close. The final sanitizer suite passes all 112 tests.

The revised 2,247,568-byte image was flashed after another private NVS backup.
Transfer hashes verified without the earlier fast-write retry. Startup reports
ELF prefix `8f1dc270a`, matching the image descriptor, successful PSRAM, panel,
touch, PWR input, pet-session and debug initialization, with intact display
guards and no observed panic. The brief monitor was closed before the debug
client read state. The nine-pet roster, active identity/slot/form, asleep state,
collection ownership and available save status match the pre-flash observation.
Engine static storage is now 30,264 bytes; session and board sizes remain as
measured above. The owner is reviewing the revised care/emergent layout on-device.
Their earlier menu/Back confirmation applies to the first image.


Final revised validation is complete: desktop 112/112, core 49/49 and sanitizer
112/112 pass. The post-refactoring focused checks also pass. Static analysis,
normal/power firmware builds and profile checks, external CLI acceptance, and
125-document validation all pass; `docs-fix` leaves no unresolved findings.
The revised firmware is running with checkpoint preservation verified. Owner
confirmation of the latest care/emergent wheel remains pending; the first-image
menu/Back confirmation must not be substituted for that final physical review.


# Completion-audit regression coverage

A final requirements audit added explicit tests for two previously indirect
cases. Per-pet invitation tests now verify active-only generation, independent
seen state, round-trip persistence of unseen and seen offers in the same roster,
and retirement of both active and inactive pets' expired offers. Pointer tests
verify present/mess priority when their bounds overlap the pet, successful object
actions without opening navigation, and cancellation when the active pet changes
during a hold. These are test-only additions; the flashed image is unchanged.

The expanded invitation/discovery tests pass on desktop and under sanitizers;
the invitation test also passes in the core build. Physical review of the latest
layout remains the only outstanding acceptance evidence.

# Reconciliation and deployment follow-up (2026-11-15)

The local snapshot, including separate battery and SDK changes, was reconciled against main commit `9c796d6` in an isolated
worktree. The merge retains the Traveling activity and enlarged Back control,
while the pet pointer path owns the hold indicator. A merged legacy hold updater
initially cleared pointer feedback; limiting it to legacy contacts restored the
three-second pointer test. The Back slot was also incorrectly classified as an
activity start; the action predicate now excludes slot zero.

Main's format 20 regression tests were adapted to the single-food and format 21
rules. A separate fixture was encoded by the actual main-based format 20 writer
with a two-pet roster and an in-flight fruit meal. The current decoder loaded it,
retained sequence and activity, and migrated the retired food variant. The fixture
is `tests/fixtures/save-v20-meal.bin` (SHA-256
`010963ec6d0a38734bb9772bb7d5bdc991190409834397861ca9ce41c932ca31`).
This exercises the older wire format; it does not prove every possible old save.

The first combined desktop run passed 119 of 120 checks. Asset reload failed
because its merged test still pressed Back after Rest had closed the menu;
removing those obsolete navigation steps made that test pass separately. The
format-20, save, pointer-engine, navigation and battery/history targeted checks
also passed. Repository hooks and documentation validation passed after fixing a
redundant hold condition and a callback-related static-analysis false positive.
The duplicate memo identifier was resolved by renaming the IDF deployment memo
to [memo-117](memo-117-esp-idf-6-1-menu-deployment.md), preserving its UUID and
creation date. The sanitizer run passed all 120 checks on the combined build; it preceded
the later stored-v20 fixture addition and the final nonsemantic hold cleanup.
A subsequent core-only run passed all 56 checks including the fixture.

The combined ESP-IDF 6.1 firmware, which also included separate power and SDK changes, built, including an incremental rebuild. The
pinned esptool 5.6 rejected the older `image-info --version 2` option; plain
`image-info` reported a valid image and application version 0.19.0. After a
private NVS backup, the reconciled image was flashed with verified transfer
hashes. USB startup reported 0.19.0, a passing PSRAM test, panel creation, touch
registration, an available save, unknown RTC and game-ready status. The reset
cause was USB UART reset initiated by the monitor. The battery worker reported
2,144 bytes free at its stack high-water mark and the display task reported 1,492
bytes free in this short capture. Remote state retained active pet 2 and nine
pets; a prior power history record remained readable. Logs and the backup are in
ignored `build/` output in the reconciliation worktree. These observations do
not establish physical display/touch, gauge accuracy, hardware sleep, current or
long-term storage behavior.

A follow-up raised the history writer's NVS reserve to include checkpoint blob
metadata as well as its payload. The low-space fault test exercised the cutoff,
the incremental firmware rebuilt and the board was flashed again after another
private NVS backup. Transfer hashes verified and the debug endpoint reported a
present battery, available history and retained previous records. The reserve
is a space check, not a guarantee against fragmentation or concurrent writes.


The Snuggle activity now has an authored minimum bond, enforced for ordinary
selection and invitations. The initial boundary tests exposed two existing
Snuggle fixtures with lower bond; updating those fixtures to represent an
unlocked pet restored the reward and creature tests. The default threshold is
a provisional choice pending owner preference.

The first Snuggle hook run also rejected an implicit string-comparison test;
the test now compares the result explicitly before the hooks are rerun.

Studio CI exposed preview scenarios using a low default bond after the Snuggle
gate. The preview now accepts an explicit bond value, and Snuggle completion
fixtures set the threshold. One first local retry also compared against the old
default bond; aligning the baseline with the scenario fixed that assertion.
The latest hosted PR revision must still pass its own checks.

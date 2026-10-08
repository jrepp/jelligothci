---
title: Linked sleep, rolling habits, and collectible presents
author: Codex
created: 2026-10-08T06:55:02Z
tags: [assets, gameplay, memo, persistence]
id: memo-018
project_id: jelligotchi
doc_uuid: ce7fe4b7-fb6f-4998-aa8a-b35c36b970f9
---

# Context

The user requested linked personal/pet sleep, a rolling Sleep score with bonuses
and penalties, slow food/hygiene decay, and celebrations only when an activity
finishes. They also identified the missing present collection screen and asked
for nine distinct, clean prize assets. This record distinguishes implemented
prototype defaults from the broader proposals in RFC-001 and RFC-002.

# Behavior and balance

Manual REST starts a linked bedtime session; WAKE closes it. Automatic naps do
not create personal records. The bounded journal retains eight records, lifetime
duration, and timestamp-validity flags. Unknown wall time records simulation
length without claiming a real bedtime. Manual sleep prevents pet switching.

Each pet retains 25 hourly bins for sleep duration, play duration, and meals.
The oldest bin fades by elapsed hour fraction for a rolling 24-hour estimate;
lifetime totals are separate. Unknown coverage has a provisional 75% prior,
which disappears over a day. Targets are eight hours of sleep, three meals,
and ten play minutes. Only the Sleep score currently modifies gain balance:
above 75% grants 20% more Social/Play, below 25% grants 20% less and increases
awake energy drain. The displayed immediate care stats remain responsive.

Prototype food loss is six displayed points per awake hour, hygiene 1.5. Sleeping
loss is 1.5 and 0.75 respectively. Completed meals cost one hygiene point;
completed play costs two. These are game balance defaults, not health guidance.

Completed activities collect their own accepted-command and completion deltas,
then animate positive stat gains sequentially. Passive decay, cheats, and touch
reactions do not start these sequences. A canceled healthy routine does not
award completion progress. Menus suspend the presentation; swiping dismisses it.

# Assets and collection

Nine original procedural 32x32 PNGs use stable IDs 11001–11009: Butterfly, Pearl
Tooth, Breakfast Sun, Tea Sprite, Movie Star, Bubble Gem, Moon Charm, Rainbow
Seed, and Friendship Bow. Each has a distinct silhouette, shared palette,
binary alpha, and export-derived bounds/centroid. They are source PNGs suitable
for later paintovers, not variants of the generic wrapped-present icon.

The asset validator checks 79 assets and produces a self-contained preview with
a dedicated nine-prize section. Raw payload is 147,488 bytes. Existing 12,288-byte
definition/metadata allowances bring the planned pack to 159,776 bytes within a
160 KiB ceiling; this is not the full firmware size. The contact sheet was
visually inspected on desktop. Physical-panel readability remains unverified.

The collection model caps ownership at one per type, retains discovery history,
and stores per-pet unlock progress. An offered prize does not become owned until
caught. The UI is a 3x3 grid with direct navigation after a catch and a latched gift for
another pet. A deterministic test covers all nine slots, catching, selection,
blocked self-gifting, successful cross-pet gifting, and retained discovery.
The SDL grid snapshot was visually reviewed. Medicine/shots grant no prize
progress, and giving a Friendship Bow cannot immediately manufacture another Bow.

# Storage and clocks

Codec v3 stays within a 4 KiB buffer and migrates v1/v2 with unknown new history.
The ESP32 adapter uses an NVS checkpoint blob, preserves unsupported/corrupt
blobs, and disables further writes instead of erasing them. Desktop keeps its
alternating files. Offline advancement is capped at 24 hours with one minute of
bounded work per resume step, using the same 100 ms simulation rules as live play.

The board PCF85063 stores UTC. A separate explicit sync marker prevents plausible
factory RTC dates from silently granting offline progress. Local offsets and
clock adjustments are persisted. Clock sync reports actual port completion;
failed writes remain failures. The console exposes clock sync, habits, and the
sleep journal. RTC battery retention and hardware sleep are not yet verified.

# Live artwork and release packaging

The desktop host accepts an optional injected asset set. The portable core keeps
its embedded fallback and performs no file IO or allocation. `make run-live`
launches a half-second PNG watcher which recomputes geometry and atomically
publishes a CRC-checked JLAP pack. SDL validates known IDs, dimensions, bounds,
centroids, and ground anchors before swapping complete banks between frames.
Invalid edits retain the last valid images; pet state and active bedtime persist.
The font and celebration sprites use the same injected source.

All 79 assets occupy 149,400 bytes in the development pack, including record
headers. The desktop loader uses a 692,560-byte startup allocation (two 215,192-byte
banks plus a 262,144-byte input buffer and bookkeeping) and no per-frame
allocations; ESP32 does not link the loader. The authoring helper and eight tests
cover atomic replacement, invalid image handling, geometry, and packaged binary
location. A live socket test verifies actual image changes without game restart.

Native release ZIPs now contain source PNGs, the manifest, preview HTML, and
live-art tools alongside the game. Packaging checks all 79 PNG bytes against the
manifest's source files and requires nine unique present PNGs. Source art is also
available separately. The README is shortened to an entry point and documentation
index; operational detail lives in the linked `docs/` guides.

# Embedded decode stack correction

Integration review found that the save decoder's local candidate required a
3,632-byte Xtensa stack frame, exceeding the configured 3,584-byte main task stack
before any caller frames. The remedy is a caller-owned decode workspace for ESP32,
while retaining the existing convenience wrapper for desktop callers. Decode
failure still preserves the output. Target compiler stack-usage output reports
80 bytes for the workspace decoder. The additional static scratch brings the
session workspace to 11,248 bytes; pet engine state is 10,440 bytes before later
small UI fields. The two PSRAM framebuffers remain 434,312 bytes each. Runtime
stack high-water and SDK callee usage still need hardware measurement.

# Validation status

Asset validation and visual contact-sheet review passed. Core tests passed 15/15;
ASan/UBSan desktop integration passed 33/33, including live artwork swaps, gallery,
rewards, debug clock, and save/resume. The watcher suite contains eight Python
cases. Clang-tidy, Cppcheck, and size checks passed after integration fixes.
Release metadata/version checks, workflow pins, actionlint, and documentation
validation passed. The native archive staging check verified every PNG. A local macOS native ZIP
passed signing verification and headless startup; an extracted copy successfully
ran the bundled art converter and loaded all 79 live assets in the packaged game.

The 884,800-byte firmware was flashed over the rediscovered `/dev/cu.usbmodem1101`
(303A:1001, serial 90:70:69:FE:21:DC). Transfer hashes matched. Startup verified
PSRAM, CO5300/CST9217/LVGL, NVS session availability, mono PCM16 audio initialization,
and USB debug readiness. Explicit clock sync completed successfully with UTC
and offset -240. The remote debug state and framebuffer confirmed all nine
collection slots; this checks native rendering, not physical panel readability.
The event viewer was reconnected on port 8766. Logs/screenshots are under ignored
`build/habits-*`. Physical touch, audio quality, and battery RTC retention remain
unverified. A tagged-version rebuild/flash and release publication follow these
checks; record their final revision separately.

# Release compiler correction

The first release-candidate validation (run 37742276429) passed portable core tests
on all three operating systems but GCC's SDL sanitizer build rejected an integer
promotion in the reward interpolation ternary. Local Clang had accepted it. The
fix assigns the terminal value directly and uses a separate, range-bounded cast
for the interpolated branch; warning flags remain unchanged. The release is held
until cross-platform validation passes. A subsequent GCC pass exposed the same
narrowing class in the new renderer test; its already-checked subtraction now
uses an explicit cast. Strict GCC 14 syntax checks were then extended across all
core and test sources, also correcting one pre-existing test format to PRIu32.

# Remaining polish

- Tune decay and sleep targets through ordinary multi-day use.
- Distinct flight patterns and opening frames can build on the shared catch path.
- Portable pet memory import/export and minigames remain proposals.
- Device power saving and physical RTC retention need separate hardware work.

# References

- [Architecture draft](../rfcs/rfc-001-virtual-pet-systems-architecture.md)
- [Pet memory and activity proposal](../rfcs/rfc-002-pet-memory-and-persistent-activities.md)
- [Previous deployment record](memo-017-release-downloads-and-expressive-slice.md)
- [Asset source inventory](../../assets/slice/README.md)
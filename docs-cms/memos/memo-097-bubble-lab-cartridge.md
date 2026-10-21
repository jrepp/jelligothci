---
id: memo-097
title: Bubble Lab cartridge and launch controls
author: Codex
created: 2026-10-21
tags: [checkpoints, collectibles, gameplay, validation]
project_id: jelligotchi
doc_uuid: 52108bdb-3980-479c-84bc-86666d6d2d2e
---

# Scope and owner direction

This increment implements Bubble Lab from the owner-directed requirements in
[RFC-012](../rfcs/rfc-012-gift-crafting-and-activity-ledger.md). The RFC remains a
draft for its other proposals. The owner subsequently requested larger cartridge
launch artwork, Plays/Wins statistics, simplified cartridge shelves, swipe-primary
controls with queued gestures, downward drop, larger medicine/shot decorations,
and background MOD music with a separate activity-music mute setting.

The game uses three authored levels with increasing stationary viruses, matching
medicine pairs, independent bottom-only support, four matching-color/symbol lines, bounded
cascades, top-out after clear resolution, and current-level retry. Final success
uses the centered pet/star celebration and returns to the cartridge shelf. The
[rule source](../../core/collectible_bubble.c) and
[bounded declarations](../../include/jelli/bubble.h) own current tuning. Same-color
pairs and repeated supply deliberately keep these first layouts approachable;
there is no random impossible well generator. The authored solution test also
recovers after an intentionally misplaced first pair.

The existing cartridge template now uses the Bubble Gem motif through the
[procedural art source](../../assets/slice/source/collectible_art.py). The recipe
reuses Pearl Tooth and Bubble Gem without consuming either; see the
[crafting table](../../core/collectible.c). Ownership stays permanent and universal
across pets. No new care rewards or special-pair discoveries are introduced.

# Input, presentation and checkpoint contract

The [launch/shelf UI](../../core/pet_collectible_ui.c) exposes large artwork as a
real launch hit target, with the primary button as an alternative. Owned launch
cards omit recipe and small explanatory text. Unowned cards retain acquisition
information. Both Chain and Bubble Lab use this consistent launcher. A different
pet/cartridge explicitly labels replacement of the shared checkpoint. This is
still one shared session; no per-game slots are implied.

The [Bubble UI](../../core/pet_bubble.c) reserves the well before side decorations,
score, lower-left cheering pet and radial Back. Directional buttons were removed
at the owner's request. Swipes enqueue bounded movement/drop actions, tapping the
well rotates, and arrows mirror gestures. The
[input controller](../../core/collectible_bubble_control.c) drains one action per
simulation tick, rejects a full queue without mutation, and discards stale queued
input on pause/retry. Queues are transient, not persisted gameplay intent.
Normal/relaxed/manual pace remains available with identical scoring. Symbols and
virus spikes supplement color. Automated rendering is not an accessibility or
physical touch validation claim.

The [save tail](../../core/collectible_bubble_save.c) packs the board into nibbles,
records level/current pair/supply cursor/score/assistance, and appends cartridge
statistics and the background-music preference without expanding save capacity.
The outer save version advances; the independent tail schema accepts the earlier
playtest schemas as well. Earlier saves default to music enabled and empty new
statistics. Unknown schemas and malformed packed cells reject the decode without
publishing partial state or discarding the user's source save.

Plays count new full runs, excluding resume and Bubble Lab's current-level retry.
Wins count once on final transition, not when rendering the celebration. Counts
are global per cartridge and saturate rather than wrap. Historical play is not
inferred. A pre-statistics in-progress run may finish with a tracked win but no
tracked start. The setting affects background music, while record playback and
sound effects retain their own behavior.

The owner also requested chill MOD backgrounds, implemented in the
[music adapter](../../core/pet_music.c) using original tracks authored by the
[background composer](../../tools/audio/create_background_music.py). Pocket Arcade
plays on an owned cartridge launch card; Garden Drift covers garden/fishing;
Tea Steam covers tea/meals/gifts; Bubble Bath covers wash/care/swim; Moon Pillow
covers sleep/quiet moments; Little Steps covers play/exercise. The
[music catalog](../../content/music.json) remains the source of track identity.
These are editable Studio tracks, not collectible records. Background tracks loop,
stop on idle/unrelated menus/game entry, obey master volume and the separate
activity-music setting, and never credit record play or rewards. Record playback
keeps its existing behavior, and cue ducking remains in the audio mixer.

# Verification and findings

The dedicated [rules/save tests](../../tests/test_bubble.c) cover authored solutions,
mistake recovery, bottom-only support, blocked rotations, horizontal/vertical
clears, cascades, victory before top-out/exhaustion, current-level retry, queued
input order/overflow/stale removal, manual pace, per-pet cartridge access, save
round-trip/migration/corruption, and idempotent statistics. The
[UI tests](../../tests/test_pet_collectible_ui.c) exercise real hit testing on
artwork and primary buttons, replacement/cancel/resume, swipes, hidden directional
controls and the music setting. The
[celebration tests](../../tests/test_pet_collectible_win.c) cover final shelf return
without repeated gameplay rewards.

Initial validation caught a reused artwork ID, a missing append-only catalog
identity, and struct-padding comparisons. These were corrected by registering a
new cartridge asset through the existing generator, locking the appended catalog
identity in the generator, and comparing Bubble fields explicitly. Corruption
and migration fixtures were extended as the save tail evolved. Strict size checks
required splitting controller/validation responsibilities; limits were retained.

Native screenshots were inspected for the well, launcher and controls. A separate
live desktop playtest confirmed actual artwork launch, Back/Resume and retry;
an apparent launch failure was not reproduced after settled navigation. The owner
then requested the launcher and gesture refinements described above. This does
not establish touch usability or physical display behavior.

Validation on the 2026-10-21 implementation worktree based on `af5c1ef`:

- `make core-test`: 46/46 passed.
- `make test`: 106/106 passed.
- `make sanitize`: 106/106 passed; focused native sanitizer checks were repeated
  after making the Bubble mutation helper's pointer ownership explicit.
- Strict C formatting, Clang-Tidy, Cppcheck and size gates passed. Cppcheck initially
  treated a nested session alias as unchanged across a helper taking the whole
  game; passing the Bubble pointer directly made the mutation contract explicit.
- `make docs-check`, then `make docs-fix`: valid; the only repair sorted memo tags.
- Music's pinned-container UI checks passed 9/9 with baseline updates disabled.
  The reviewed accessibility-tree changes add only the six new song options.
  Native/preview exports matched exactly for all thirteen tracks. The first export
  run caught an old single-digit record-only CLI range; its parser now validates
  bounded one/two-digit indices against the authored track count.
- `make esp-build`: passed using the existing pinned SDK/tool installation with a
  fresh worktree build directory. The application image measured `0x1f9a80` bytes,
  with 51% of the app partition free. The initial attempt stopped before compilation
  because this isolated checkout had no SDK link; using the already-installed
  pinned tools resolved that environment issue without changing pins.

Measured native working storage is 78 bytes for Bubble and 116 for the shared
session. The full authored-capacity fixture measured an 8,080-byte game and a
7,458-byte encoded save within the existing 8,192-byte save capacity. The current
versioned Bubble/statistics/music-preference tail occupies 53 encoded bytes.
These are dated build/test measurements, not permanent tuning declarations.

The final live desktop was left on the Bubble launch card. Real artwork presses,
Back/Resume, retry and Settings background-music off/on all succeeded. Native
framebuffer checks additionally verify the shelf's bottom Back disk and label;
the label region contains pale text pixels over the teal control. No device was
flashed. Physical display/touch/audio behavior, human fun/accessibility trials and
musical listening preference remain separate, unverified evidence.

# Relaxed automatic-fall freeze correction

Live desktop playtesting of `482d9fa` exposed a freeze with storage reporting
`JELLI_STORAGE_INVALID` while the debug interface remained responsive. The relaxed
fall timer reached its exclusive validation bound before calling the public drop
function. That function rejected the transient invalid state, leaving subsequent
simulation and saves unable to proceed. Normal timing and manually initiated
drops had not exercised this boundary in the initial tests.

The timer now resets before entering the validated drop operation. A regression
in the [Bubble tests](../../tests/test_bubble.c) first reproduced the invalid game
state, then passed with the correction. It advances both normal and relaxed runs
through repeated automatic falls and verifies game validity and save encode/decode
on every tick. Focused desktop and sanitizer rules, save, launch and celebration
checks each passed 6/6; the incremental firmware build and documentation checks
also passed.

The live process was inspected separately with LLDB: the preserved running board
had score 180, supply cursor 8, relaxed assistance and timer 16. After backing up
the engine memory, a guarded repair reset only the timer, paused the session and
requested a save. The debug interface then reported `SAVED`, and the save slot's
modification time advanced. The desktop could then restart with the corrected
binary and preserved board. This recovery evidence does not establish physical
device behavior or replace the automatic-fall regression. The corrected desktop
then resumed this recovered relaxed session for 35 simulation ticks, remained
responsive, reported `SAVED`, and returned through Back to the Resume launcher.

# Music catalog layout regression

PR screenshot checks caught longer background-track options expanding the native
music selector and shifting adjacent controls. Earlier focused UI runs had not
enabled screenshot comparison. The [music UI](../../tools/jelli-art/music.js) now
bounds the entire song field to its prior footprint, allowing it to shrink with
its container. Bounding only the select left its grid label's intrinsic width
unchanged; the screenshot test exposed that incomplete first correction. Existing
screenshots and comparison thresholds remain unchanged.
The pinned-image music UI run with `JELLI_UI_SCREENSHOTS=1` passed 9/9, including
desktop and phone screenshot comparisons and tablet accessibility/layout checks.

# Owner-directed presentation and game feedback follow-up

The owner requested mean-looking viruses, removal of bottom help text, larger
left-side score/level/virus counters, and a larger well. The
[Bubble renderer](../../core/pet_bubble.c) now uses a larger cell grid within the
circular display, with brows, eyes, teeth and spikes alongside color symbols.
Tap hit-testing follows the enlarged well. The watching pet holds the existing
medicine and shot sprites and shakes them on actual virus removal or level clear,
using its existing reaction timer rather than adding checkpoint fields. Reduced
motion retains static props and expressions. The pace assist remains accessible
on the right; the bottom contains the radial Back control without help text.

The owner also requested shared movement/drop/clear sounds and automatic endings
for Bubble Lab and Jelli Chain. Gameplay cues follow successful simulation changes,
not queued input admission or repeated rendering. Rejected wall moves stay silent;
virus removal takes priority over the landing cue. Distinct simulation outcomes
bypass the menu-tap debounce, including the once-only loss cue immediately after a
move. Chain keeps its eat/win cues and now sounds on successful movement.

The [shared terminal sequence](../../core/pet_collectible_win.c) centers the happy
pet for a short star celebration on final victory. Loss keeps the pet in place
with an unhappy expression and tears, then returns automatically to cartridges.
The owner initially requested approximately 2–3 seconds for victory and one second
for loss, then explicitly doubled both sequences to five and two seconds. Current
durations live in the source. Intermediate Bubble levels continue play.
Reopening a lost Bubble session retains the existing current-level retry semantics;
no new loss/replay menu, care rewards or checkpoint slot was added.

Bubble Rush is an original four-channel gameplay MOD authored by
[the dedicated composer](../../tools/audio/create_bubble_music.py), using the
existing synthesized instrument bank. The [music adapter](../../core/pet_music.c)
routes it only during active, unpaused Bubble gameplay, respects the activity-music
preference, stops on terminal states, and awards no record credit. The native
export was checked against preview output; pinned-image music UI checks with
screenshots enabled passed 9/9 without relaxing comparisons.

Focused native rules, UI, ending, engine and navigation tests passed 6/6. New
checks cover expanded-well taps, rejected-move silence, consecutive movement cues,
drop/virus-clear priority, quiet repeated polling, loss cue debounce, automatic
return timing, and unchanged play/win counts on loss. Native screenshots were
inspected for the circular layout, readable counters and held props. Physical
display/touch/audio behavior remains unverified.

The expanded music routing and UI feedback initially crossed the strict function
complexity budget. Named private helpers separate track selection, reaction timing
and Bubble sound feedback; limits remain unchanged. The size gate and focused
tests were rerun after extraction. Clang-Tidy also caught duplicated loss-return
branches in Chain feedback; a standalone early guard preserved behavior, and the
three affected tests plus strict hooks passed before commit `94f53e1`.

The duration follow-up retains the same transitions and one-shot cues. Timing
regressions verify that both games remain on screen beyond the previous deadline
and return at the new boundary; paused-clock and reload coverage remains in place.

The owner requested a subdued low-pass rasp for Chain movement. The existing cue
programs had no equivalent, so the [portable synthesizer](../../core/sound.c) adds
an appended cue without changing existing IDs. A brief low saw/noise source uses
a bounded integer low-pass filter and lower gain. The cue reuses otherwise-unused
formant state, preserving the synth state budget and requiring no sample asset.
Only Chain movement uses it; Bubble movement and the other game cues retain their
programs. Debug audition and the [preview exporter](../../tools/audio/preview.py)
include the new name. Tests verify block continuity, envelope ends, duration,
reduced high-frequency sample steps, muted/master gain and unchanged cue IDs.

The owner's workout music request adds Iron Pulse, authored by the
[workout composer](../../tools/audio/create_workout_music.py). The original MOD
uses synthesized distorted kick/bass, acid-style stab and percussion, routing
only to exercise through the existing background-music switch. It follows the
activity-music preference and master volume and adds no collectible record.

The owner subsequently requested automatic cartridge insertion. The
[launcher](../../core/pet_collectible_launch.c) presents the centered cartridge,
enlarged title and Plays/Wins, slides it into a slot, emits a shared click cue and
launches after three seconds. Reduced motion uses static art and seated status.
Back cancels before gameplay or statistics mutate; replacing a different saved
session still requires explicit confirmation. This transition uses transient UI
state only and leaves the save schema unchanged.

Final follow-up validation included a fresh ESP32 firmware build with the launcher
source registered in the port. The image measured 2,114,528 bytes in the 4,194,304-byte
application partition, leaving 2,079,776 bytes free. Native `sizeof` measurements
against the prior committed UI header showed `JelliPetUi` increasing from 9,960 to
9,984 bytes and `JelliPetEngine` from 20,584 to 20,608 bytes; these are desktop ABI
measurements, not ESP32 structure sizes. The synthesizer remains 24 bytes, and the
launcher adds no serialized fields, heap allocations or framebuffers. Final focused
launcher/music tests passed 3/3, sanitizer sound/music/launcher/ending/engine checks
passed 5/5, and music Studio screenshot checks passed 9/9. No device was flashed.


# PR 81 timed-launch integration coverage

[PR 81](https://github.com/jrepp/jelligothci/pull/81) CI failed the existing
`pet_gallery` crafting test after automatic insertion was added. That test still
expected a crafted cartridge to become active immediately after its launch tap.
The same assertion failed locally before correction. The preceding focused
launcher/music and sanitizer runs passed but omitted this gallery entry path;
they were not evidence that the complete integration suite passed.

The [gallery test](../../tests/test_pet_gallery.c) now advances the injected clock
through the gallery update function, checks that the session, revision, play
count and pending gift remain unchanged before the three-second boundary, then
checks activation and exactly one counted play at completion. A later update
must not count or mutate it again. Runtime launch behavior is unchanged. Other
cartridge tests were audited: dedicated launcher tests already advance time;
engine, music and ending fixtures use direct engine setup, which does not have a
UI insertion delay. Ordinary present playback remains immediate.

The reusable [change-validation guidance](../../CONTRIBUTING.md#make-and-review-a-change)
now requires timed-transition checks across entry paths and surrounding integration
coverage, including craft and resume. Physical display/touch/audio behavior is
not established by this test-only correction.

After rebuilding both configurations, all 70 native C tests passed on desktop
and all 70 passed with sanitizers, including gallery, launcher, ending, engine
and SDL smoke/demo coverage. Native tests were selected from CTest's JSON inventory
by executable location in each build directory. Unchanged Python/Studio suites
were excluded from this local correction run; the earlier CI failure reported
only the gallery assertion. Documentation validation and repair checks passed
without repairs, and the C size gate passed. Required PR checks still need to
pass on the corrected revision before merge.

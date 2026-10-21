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
The owner requested approximately 2–3 seconds for victory and one second for loss;
current durations live in the source. Intermediate Bubble levels continue play.
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
tests were rerun after extraction.

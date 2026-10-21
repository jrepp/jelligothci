---
id: memo-098
title: Encountered tune library and rare record distinction
author: Codex
created: 2026-10-21
tags: [collectibles, music, saves, validation]
project_id: jelligotchi
doc_uuid: 240d83af-baee-4680-814d-d727e01b3f83
---

# Owner direction and implementation

The owner requested all tunes encountered during play in the record area,
alongside rare collectible records, then specified a golden border for rare
records and morale boosts exclusively for those records. This follow-up uses the
existing [MOD catalog](../../content/music.json) without changing arrangements,
samples or acquisition recipes.

The [library shelf and replay](../../core/pet_tune_library.c) retains rare record
cards and appends encountered background tunes. Golden borders identify rare
records, including locked acquisition cards. Plain tune disks launch directly into
the existing café, with the active pet's happy poses and drifting music notes.
Previous/Next and Back retain standard positions. Existing owned record playback
and its bounded benefits remain unchanged.

The [music adapter](../../core/pet_music.c) records logical encounters before
background mute/audio-driver decisions, independently of successful audio
callbacks. Only the first encounter mutates the bitset and requests a save.
Paused/offline recovery frames grant no discoveries. The fixed-width bitset uses
append-only music IDs, with a compile-time capacity assertion; catalog names come
from the [music compiler](../../tools/audio/build_music.py), which rejects an
order differing from the stable compiled IDs.

The [save codec](../../core/collectible_save.c) appends two bytes under its new
version and accepts earlier versions with an empty encounter mask. Historical
tunes cannot be reconstructed from old saves. Revisit an activity to encounter
its music. Rare-record and unsupported bits are invalid, and failed decodes leave
the caller's save untouched.

Library replay uses transient UI state and the injected animation clock, including
silent/no-driver playback. It preserves collectible checkpoints and has no path to
morale, rare unlocks, gift ownership or play-time counters. Focus loss pauses it;
sleep/care, closing the player or changing pets cancels it. A process restart
retains discovery but starts library playback afresh. No offline playback credit
or new reward policy is introduced. The [development contract](../../docs/development.md#encountered-music-library)
owns these boundaries.

# Validation findings

Initial full core, desktop and sanitizer runs exposed seven existing legacy
migration/corruption fixtures whose shared offsets omitted the new tail. The new
version round-trip and v18 migration tests passed, but those older fixtures were
constructing invalid records. Updating [the centralized layout](../../tests/save_layout.h)
restored their intended byte positions without relaxing codec validation. The
[contributor guidance](../../CONTRIBUTING.md#make-and-review-a-change) now requires
updating that layout and checking old migrations/corruptions with a save extension.

The strict size gate required separating render-key comparison and the pet-cheering
predicate; limits remain unchanged. An initial source split lacked its collectible
API include, caught by the compiler and corrected. A no-reward test initially
compared before ordinary UI clock synchronization; the fixture now synchronizes
the clock before taking its snapshot, then verifies the complete game state stays
unchanged through library replay and Back. Clang-Tidy rejected comparing the
native game struct because padding is not a portable equality contract; the
regression now compares encoded save bytes and revision explicitly.

Native screenshot inspection caught small card labels meeting the golden border;
title/label spacing was adjusted and fresh fixtures were inspected. Rare borders,
locked/available labels, tune cards and café playback are readable at native
resolution. This is desktop rendering evidence, not physical display/touch or
audio-listening evidence. No live desktop process, user save or physical device
was modified.

PR84 CI then exposed a strict GCC `-Wconversion` failure in the tune timer:
compound assignment promoted the addition to `int` before implicitly narrowing
back to `uint16_t`, despite a cast on the increment. Earlier AppleClang desktop
and sanitizer builds had accepted that expression. Debian GCC 12.2 reproduced
the exact failure with the project warning flags. The remedy explicitly casts
the final duration-bounded sum at assignment, preserving the timer behavior.
The regression now also overshoots the duration to check clamping; existing
coverage checks the exact boundary and pause/resume behavior. Both corrected
source and test compile with those GCC flags, and the four focused tune-library,
music, gallery and collectible-UI tests pass in desktop and sanitizer builds.
The updated PR revision still requires its own CI result; local verification
alone does not establish that the required checks pass.

# Verification and resource measurements

- Core configuration: 47/47 tests passed after the shared fixture correction.
- Complete desktop and sanitizer runs each passed the other 101 of 108 tests;
  their only failures were the seven legacy-fixture cases described above.
  After correction and rebuilding, all 72 native C tests passed in each
  configuration, including those cases, the full gallery/launcher integration,
  mute/no-driver encounters, silent replay, rare rewards, and byte-for-byte
  gameplay-state preservation through library playback and Back.
- Catalog compilation and native/preview audio export tests passed in the full
  runs; no arrangements or samples changed in this increment.
- Pinned formatting and all C size checks passed. Documentation validation and
  repair checks passed without repairs.
- The fresh ESP32 firmware build passed. The image measured 2,117,360 bytes in
  its 4,194,304-byte application partition, leaving 2,076,944 bytes free.
  No board was flashed or physically tested.

The full authored-capacity save fixture measured 7,460 encoded bytes within the
unchanged 8,192-byte capacity, a two-byte increase. Native ABI measurements were
8,080 bytes for `JelliGame` (unchanged through padding), 10,016 for `JelliPetUi`
and 20,640 for `JelliPetEngine` (32 bytes larger each). The transient tune state
is 24 bytes; added render-key state accounts for the remainder. These are dated
desktop ABI measurements, not ESP32 type sizes. No new heap allocations, sample
buffers or framebuffers are introduced.

# Studio game-page snapshot coverage

The optional Studio UI run [39579237822](https://github.com/jrepp/jelligothci/actions/runs/39579237822)
on candidate `519b032` failed exactly the desktop, tablet and phone game-view
ARIA comparisons; its other 314 tests passed. The library's `JELLI_UI_TUNE_PLAY`
page intentionally adds a “tune play” option because
[Studio's catalog](../../tools/jelli-art/game_preview.py) reads the C page enum.
The original feature validation omitted these Studio cases because no browser
source changed. Native game tests do not cover this generated browser contract.

The remedy adds that single option after “collectible play” in each of the three
ARIA snapshots. No screenshot baselines, accessibility exclusions or application
behavior changed. [Contributor guidance](../../CONTRIBUTING.md#jelli-art-packaging-and-pr-validation)
now calls for focused embedded-game UI tests when C page enums change.

The focused `test_view and game` selection passed all four selected cases in the
pinned Playwright container with `JELLI_UI_SCREENSHOTS=1`: all three viewport
comparisons and the related game accessibility case. Update mode was disabled,
so existing screenshots were compared without being rewritten. This is browser
rendering evidence, not physical device evidence. The pushed fix still requires
its own CI results, including the Studio UI job, before the follow-up release;
prior published tags remain unchanged.

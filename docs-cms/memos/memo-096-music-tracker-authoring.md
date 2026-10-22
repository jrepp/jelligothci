---
title: Bounded MOD tracker authoring and four-channel playback
author: Codex
created: 2026-10-21T00:00:00Z
tags: [audio, authoring, memo, validation]
id: memo-096
project_id: jelligotchi
doc_uuid: c1309808-286b-4419-981d-d2e8e631a5fc
---

# Requested increment and implementation

The owner requested a Music sub-application in Jelli Art, then clarified that it
should be a tracker with four PCM channels, editing hotkeys, standard MOD files,
and a sample manager. The owner subsequently clarified that a full runtime MOD
loader was unnecessary: the editor should expose the actual player constraints.
This supersedes the earlier three-voice implementation described in
[memo-094](memo-094-morning-tea-record-audio.md).

The canonical authored records are [MOD files](../../content/music/), with original
starter arrangements reproduced by [create_music.py](../../tools/audio/create_music.py).
The [codec](../../tools/jelli-art/music_mod.py) accepts the bounded four-channel,
31-sample ProTracker `M.K.` subset. It rejects unsupported effects and finetuning;
no import silently drops unsupported commands. Supported operations are notes,
sample changes, volume, loops, `Cxx` volume and `Fxx` speed/tempo. Authoring and
sample limits are checked by the codec before writing or compiling.

The [tracker UI](../../tools/jelli-art/music.js) provides a four-channel pattern
grid, note entry, row-step and octave controls, order editing, pattern copy/paste,
undo, keyboard-accessible cell fields, MOD import/export, and a waveform sample
manager. Sample replacement accepts unsigned eight-bit mono WAV and signed
8-bit PCM. MOD has no sample-rate metadata; the UI explicitly explains tracker
pitch. Sample and total-memory limits apply to imports. Four independent browser
gain/mute controls mix the audition stems without changing saved sample volumes.

Saves use the studio write lock, atomic replacement and loaded-file hash. Conflicts
require the existing explicit overwrite flow. Draft recovery preserves unsaved
modules, including the selected record. Playback stops on workspace exit and
browser hiding. The browser audio context is resumed directly from a user gesture
before awaiting the preview request.

# Runtime boundary

The [build compiler](../../tools/audio/build_music.py) validates MOD input and
emits immutable PCM and bounded event spans. The [C player](../../core/music.c)
never parses MOD data, allocates, or reads files. A START command atomically selects
its record and checkpoint sample cursor. Each block binary-searches the four
channel spans at the cursor and advances bounded indexes; seeking does not replay
previous audio. The fixed record window remains thirty seconds and repeats the
order list as necessary. The public API and state contract are in
[music.h](../../include/jelli/music.h).

The browser preview renderer and C player use the same integer sample phases,
PAL timing and sample data. This is a constrained nearest-neighbor renderer, not
a promise of indistinguishable output from every historical Amiga filter or MOD
player. Independent compatibility was checked against libopenmpt; its output is
not used by the runtime.

# Verification and findings

On the integration worktree, the five Chromium tracker tests passed at desktop,
iPad-sized and phone widths. They exercised keyboard note entry/navigation, draft
recovery, save persistence, concurrent external-file conflict cancellation, live
preview and pause/stop, sample replacement, MOD export and pattern editing. The
Music subtree had no axe violations in that run. These are browser tests, not
physical touchscreen or speaker evidence.

The MOD codec's six tests passed, including bounded malformed input and signed
PCM, supported effects, round-trip equality and optional independent decoding.
Installed libopenmpt 0.8.9 identified all seven starter files as ProTracker `M.K.`
modules with four channels and rendered nonzero audio. The starter modules measured
5,020 bytes each, with 1,888 PCM sample bytes each at this revision. Regenerating
into a temporary directory reproduced every module byte for byte.

Native exports for all seven records matched browser-reference PCM byte for byte
for all 661,500 samples per record. Native strict compilation, sanitizer tests and
clang-tidy passed for the music layer. Synthetic tests cover four-channel mixing,
looping, volume changes preserving phase, speed/tempo timing and retrigger behavior.
The runtime player state measured twelve bytes on the host. These findings do not
establish physical device audio quality or output latency.

The backend tests found that a truncated WAV body could be accepted despite its
header claiming more samples. The importer now checks actual bytes against the
frame count; its regression passes. Hash-conflict/concurrent-writer, read-only,
new-file, file-isolation, base64, import-budget and four-stem preview tests pass.
New JavaScript introduced no type-check baseline entries. Static container-layout
validation includes the shared compiler and codec without a local image build.

The Music tab changes the studio navigation. The pinned ARM64 browser image
passed all 32 view cases while refreshing snapshots; the four Music cases were
then refreshed again after the final layout/accessibility fixes. Screenshot
review confirmed the full-width desktop workspace and visible active tab. All
four Music axe baselines are empty, with no new nonempty exceptions elsewhere.
The final five functional tracker tests passed again. Current-head CI remains
the final PR check; local results do not establish that required checks pass.


A later full host run found that the real-game preview staging tree copied the
MOD compiler but omitted its codec dependency, causing CMake configuration to
fail. The preview's explicit staged-file list now includes the codec. Its normal
byte comparison also invalidates the cached build when the codec changes; served
content and the audio tool directory already participate in staging. The full
game-preview suite passed all 27 tests after this repair.


Two overlapping validation runs reached the server suite's watchdog during a
late `/api/data` lint-error test. Running the suite alone afterward passed all
36 tests in 220.515 seconds, including that case. This did not reproduce a server
deadlock; CPU contention is suspected, but not proven. CTest now runs that suite
serially without increasing its existing timeouts. An isolated local pass does
not establish current-head CI success.


# Mobile authoring and sample presets

The owner requested mobile tracker authoring improvements and delegated a new
sample set. The [tracker](../../tools/jelli-art/music.js) now provides a touch
note pad, entry-instrument selection and row/channel navigation. Phone layouts
keep readable cells in a horizontally scrolling grid with larger touch targets.
Record settings, file operations, arrangement controls and keyboard help use
expandable sections; focus, expanded sections and grid scroll survive edits.

The [preset generator](../../tools/audio/music_samples.py) adds original looped
melodic sounds and one-shot percussion/plucked sounds without rewriting existing
records or changing the legacy sample bank. The sample manager replaces a chosen
slot explicitly, checks the combined PCM budget and supports undo. Presets travel
through the existing Music API and are packaged with the studio's audio tooling.

A validation run overlapped edits to the API and its imported module and observed
an AttributeError for the new preset function. This was an inconsistent live
working-tree run; validation was restarted after both files were stable. An
initial phone accessibility pass also found abbreviated channel button text did
not match its accessible label. Full channel labels resolved that finding without
adding an accessibility exception.

The final focused pinned ARM64 Chromium run passed all fourteen Music cases,
including finger-event note entry at narrow phone and tablet sizes, saved note
pitch/instrument, focus and scroll preservation, preset replacement/undo and
persistence, conflict cancellation and existing audio preview. Reviewed phone
and desktop screenshots and all three viewport accessibility snapshots were
updated for the intended controls; Music axe baselines remain empty. These are
browser emulation results, not physical touchscreen or listening evidence.

The sample work passed the Music API, compiler and MOD codec suites. It verified
bounded deterministic data, MOD round trips, distinct nonzero rendered audio,
one-shot decay and loop tails. Legacy generator output remained byte-identical.
An ARM64 image build and real-entrypoint smoke passed health, studio HTML, asset
data and the Music preset response; the temporary container and image were
removed. JavaScript type checking added no baseline errors, and static container
layout validation passed. These local results do not establish current-head CI
or deployment success.

All eleven Studio CTest suites passed via
`ctest --test-dir build/desktop --output-on-failure -R jelli_art -j 1`, including
server saves and game-preview staging. The standalone tidy suite also passed.

The complete pinned ARM64 browser run,
`JELLI_UI_SCREENSHOTS=1 tools/jelli-art/ui_tests/run.sh -q`, passed all 321 cases,
including screenshot comparisons and accessibility checks. It emitted twelve
Pillow `getdata()` deprecation warnings from the existing screenshot comparison
helper; these remain follow-up maintenance, not failed checks. Documentation
validation and repair both passed without repairs.


## Expanded library and mobile sound selection

The owner subsequently requested more PCM samples and a selection flow integrated
with mobile note editing. The [library](../../tools/audio/music_samples.py)
adds categorized bass, lead, keyboard, mallet and percussion sounds. The expanded
bank measured 16,448 PCM bytes across 24 presets in this revision; the original
preset sounds and legacy song generator output were preserved.

The inline sound browser sits beside Entry instrument. Category/search filtering
and octave-aware audition do not dirty the document. Authors can add a sound to
an unused empty slot or explicitly replace the selected instrument throughout
all patterns. Referenced empty slots are reserved. Both actions check memory,
support undo, close the browser and focus the note pad; unused empty slots no
longer fill the entry-instrument menu. Advanced sample editing remains in the
sample manager. Browser audio startup now checks cancellation after resuming
its context, and focused buttons retain native Space activation.

The initial follow-up focused run passed seventeen Music cases, including
search/no-results, category filtering, preview without edits, unused-slot
allocation and saved note entry at phone/tablet widths. The earlier full-suite
results above predate this expanded picker; final revision checks are recorded
with the publication evidence.

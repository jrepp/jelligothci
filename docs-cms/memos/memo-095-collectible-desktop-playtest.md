---
title: Collectible desktop playtest and debug ownership edits
author: Codex
created: 2026-10-21T05:50:00Z
tags: [debug, gameplay, memo, validation]
id: memo-095
project_id: jelligotchi
doc_uuid: e958cfce-dc7e-4cec-ab23-912da36b1a67
---

# Observation and remedy

The first desktop playtest launched both activities but neither advanced after
window focus loss. Live debug state showed the global engine pause still set,
while the collectible itself was active and unpaused. The host had emitted a
Space-style pause toggle on focus loss without a matching resume path.

[SDL input](../../ports/sdl/keyboard.c) now pauses the collectible and opens its
bottom menu on focus loss. Resume uses the collectible command; explicit Space
pause remains independent. The regression test verifies movement before focus
loss, a stationary checkpoint while paused, and movement after Resume.

The owner requested simpler players, a bottom Menu control, no directional
buttons, animated actual Jelli segments, and a cheering pet three times larger.
The [player UI](../../core/pet_collectible_ui.c) now starts/resumes directly and
shows only Resume/Replay and Exit in its menu. Chain accepts swipes and desktop
arrow keys. The [renderer](../../core/pet_collectible_session_draw.c) reuses
creature frames and cached opaque bounds; larger pets do not cover controls.

# Debug ownership

The [gift handler](../../core/debug_gifts.c) supports grant/remove by catalog key
or all on either debug transport. Removal preserves discovery and earned play
rewards, clears a matching checkpoint, and stops active music. Unknown keys,
malformed commands, capture and recovery are rejected. Mutations request a save
and emit cheat events. Commands and semantics are in
[the playing guide](../../docs/playing.md#collectible-debug-edits).

# Verification

The integrated desktop build and nine focused tests passed, covering gameplay,
menus, audio bridge/output, ownership edits, focus loss and keyboard mapping.
An actual desktop socket grant/remove-all roundtrip succeeded. During record
playback the consumed sample cursor advanced from 8704 to 31232 over one second;
removing that record stopped its session. Chain's head advanced from cell 28 to
29; bottom Menu paused it and Resume continued. All collectibles were restored
through the debug port for the owner to playtest.

Native previews were inspected. The first larger-pet layout exposed clipped
footer text near the circular edge; redundant footer copy was removed. This
establishes desktop rendering and audio callback progress, not subjective audio
quality or physical display/touch behavior. No firmware was flashed.
# Iterated gameplay and collection

The owner's subsequent playtest requests are implemented in the
[Chain controller](../../core/collectible_chain.c) and
[session renderer](../../core/pet_collectible_session_draw.c): slower steps,
a two-turn input buffer, Lilac head, varied creature animations, a larger board,
quieter grid, garden props, length below score, and pickup cheers. The record
scene adds a jukebox and cactus. Native previews were inspected after these
changes; focused UI, input, debug, audio bridge, and checkpoint tests passed.

The [catalog](../../content/prizes.json) adds the requested musical styles as
records, with reusable artwork and recipes recorded in the
[gift ledger](../rfcs/rfc-012-gift-crafting-and-activity-ledger.md).
The [music studio memo](memo-096-music-tracker-authoring.md) records authoring and
MOD interoperability evidence. Gift ownership now uses a 32-bit mask with a
bounded catalog capacity; save v17 retains the old low-bit/origin layout and
appends the new fields. Tests exercise ownership beyond the old 16-bit boundary,
old-save migration, and queued-turn checkpoints. Measured native sizes after
integration: session 36 bytes, per-pet collectible progress 44 bytes, complete
JelliGame 7,992 bytes; the existing 8 KiB budget remains enforced.

# Benefits during play

[Activity rewards](../../core/activity_rewards.c) roll the complete benefit at
acceptance, including authored randomness and applicable preference modifiers.
The first portion is immediate, followed by roughly regular pulses with bounded
timing and amount variation; the final portion arrives at completion. Cumulative
integer partitioning preserves the total before normal meter saturation. The
small saved reward totals and pulse index prevent rerolls or duplicate payments.
Food/gift inventory and play costs are charged on acceptance so cancellation
cannot repeatedly collect a free opening benefit. Legacy saves retain their
completion fallback. Targeted tests cover exact totals, varied pulses, save/reload,
offline parity, cancellation, and malformed checkpoints.

[Record benefits](../../core/collectible_rewards.c) similarly arrive in pulses
based on consumed audio and permanent listening progress. Restarting, swapping,
or removing/regranting a record cannot repeat its earned benefit. Audio pause
stops progress. No physical device or subjective listening verification is
implied by the host checks.

# Validation findings

The expanded prize validation fixture exposed expensive repeated parsing of the
entire asset manifest, causing its existing timeout under concurrent host load.
The [registry generator](../../cmake/JelliPrizes.cmake) now indexes artwork once;
the unchanged-timeout fixture passes, retaining malformed-ID and identity checks.


# Owner-directed presentation patterns

The owner refined navigation to standard bottom radial Back for both players,
radial Previous/Next on the Records shelf, direct launch of an owned record,
large disk artwork and centered names without repetitive Play labels. The jukebox
now uses a café vignette, existing animated music-note sprites, and restrained
progress without seconds or a subtitle. Chain's Lilac head has no redundant
yellow ring. A win floats the pet to center for a star extravaganza, then returns
to the cartridge shelf; its presentation timer cannot award gameplay benefits.

These requirements are consolidated in the
[feature design patterns](../rfcs/rfc-012-gift-crafting-and-activity-ledger.md#owner-directed-design-patterns),
including their application to future games and their review criteria. Unapproved
games and balance suggestions remain Draft.

The broader tests exposed completion consumers interpreting each reward pulse as
an activity completion. Reward accounting now accumulates pulses while delaying
prizes until the activity becomes idle; gift celebrations likewise detect that
completion transition rather than an inventory decrement that now occurs at
acceptance. Regression tests verify no premature prize or gift celebration.

Standalone C analysis also needed the generated MOD header. The owning
[lint entrypoint](../../scripts/c-lint) now generates it alongside asset IDs, so
analysis does not depend on a previous desktop build. Debug-client transition
waiting now respects its configured timeout; real-host integration tests allow
more time for sanitizer rendering under concurrent host load.


Final integration verification included strict desktop and ESP32 builds, all core
tests across the original run and corrected expectation reruns, and targeted
sanitizer reruns for the changed players, activity consumers, win sequence, and
debug transport. The authoring preview suite passed after staging the MOD codec;
a concurrent edit could legitimately invalidate its rebuild-cache assertion, so
that check was repeated against settled sources. Commit hooks found a possible
zero divisor in static analysis of the pulse scheduler; an explicit guard now
protects the denominator even if future schedule changes permit no pulses.

The refreshed desktop loaded its earlier checkpoint and granted all catalog
items through the actual debug socket. The record shelf reported 18 total owned
gifts, with radial Previous/Next centers at (61,233)/(405,233). Selecting Garden
Waltz opened playback directly; consumed samples advanced from 13,824 to 29,184,
and Back returned to the record shelf. These observations establish live desktop
behavior, not physical-device display, touch, or subjective audio quality. The
firmware was built but not flashed.


# Chain sound feedback

The owner requested reuse of existing sounds. The
[feedback bridge](../../core/pet_feedback.c) observes live Chain transitions:
happy for a pickup, sparkle for the winning star, and the soft Back tap for a
collision. Ordinary movement stays quiet. Comparing session identity, generation,
and running state prevents sound replay when restoring a checkpoint, remaining
on a won/lost screen, or pausing. The existing host sound path retains master
volume and mute behavior; no samples or audio driver were added. Engine tests
exercise actual pickups, movement, wins, pause/resume, and collisions through the
same frame and sound-consumption path used by the hosts.

The sound regression's first static-analysis run rejected implicit widening of
its test-clock multiplication. Using an explicit 64-bit time operand retains
the expected duration and follows the portable arithmetic contract. Desktop and
sanitizer behavior checks and the firmware build passed before that test-only
correction; commit hooks verify the corrected arithmetic.

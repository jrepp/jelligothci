---
id: rfc-002
title: Pet memory, persistent activities, gifts, and minigames
status: Draft
author: Codex
created: 2026-10-08
tags: [activities, gameplay, memory, persistence]
project_id: jelligotchi
doc_uuid: a94cdd7a-99c9-44c9-bccb-eacd3498472c
---

# Summary

Record the requested next increment while the current slice is stabilized and
flashed. This is a design proposal, not a claim that these systems are implemented.

# Checkpoints and pet-owned memory

Use compact, explicitly encoded checkpoints rather than SQLite for the current
small bounded state. The existing codec fits within 4096 bytes and uses a schema
version, sequence, declared length, CRC, and commit marker. On ESP32, evaluate two
NVS blob slots using the repository-pinned SDK: NVS has wear leveling and is
intended for small values. Write an immutable snapshot on a storage worker,
coalesce routine changes, and checkpoint important transactions. Never write
flash per animation frame or touch particle. Do not erase NVS on a load error.
Current firmware still has a volatile session; desktop saves are implemented.

Give every pet a stable identity and a portable `.jellimemory` record containing
its authored personality seed, bounded learned affinities, care history, meaningful
milestones, and schema/content versions. A pet can ship with a unique authored
seed file. Its evolving memory is a separate writable record; loading a fresh
seed must never silently erase learned history. No framebuffer, menu animation,
particle pool, pointers, or native C structure padding belongs in the record.
Loading must validate identity and ranges before replacing the live pet.

Checkpoint shared inventory and all changed pet memories as one generation.
Independent file writes alone cannot safely transfer an item between pets: use a
manifest/commit record naming verified component generations, or put small records
inside one atomic checkpoint blob. The portable pet file remains an import/export
boundary. Initial resource target: 512 bytes per memory record, eight pets, no
steady-state allocation; measure NVS capacity before choosing a larger partition.

# Compatibility

Use explicit little-endian fields, never dump C structs. Maintain reader-version
bounds and bounded, length-prefixed sections. Migrate known older schemas with
explicit defaults; reject unknown required sections. Preserve unknown optional
sections if round-tripping them is supported; otherwise open read-only and retain
the original file. Never overwrite a newer, unsupported checkpoint with a fresh
pet. Keep golden fixtures for every supported schema and test truncation, CRC
failure, duplicate IDs, power loss between slots, downgrade, and re-upgrade.
The current desktop codec writes version 2 and reads version 1; forward decoding
is not promised. Format versions are independent of game release versions.

# Health history

Shots vary from one to three taps. Persist the chosen goal and completed taps;
leaving the screen cannot reroll the round. One hour after the first accepted
shot begins the next eligible round; while a round is unfinished its remaining
taps remain available, then the option disappears. Medicine is always one small
dose, with a small need effect and a cooldown to prevent repeated dosing by
reopening the screen. Current implementation uses a one-hour gameplay cooldown.
These are fictional pet mechanics, not real-world medical guidance.
Use monotonic pet time for cooldowns. A trusted wall-clock delta may advance
checkpoint time under a bounded offline policy; backward or unknown time must
not shorten a cooldown. A reboot must not reset history once device saves land.

# Food, drinks, and alternate screens

Proposed activity menus: Breakfast offers food; Tea offers drinks; Movie offers
snacks and drinks; Going out offers food and drinks. The user was asked to confirm
this mapping. Each choice needs an explicit cost, need effect, preference effect,
full/busy/asleep rejection, and readable event. Reuse one selection layout and a
small icon set rather than authoring a separate scene for every combination.

Settings is the single place for pet selection and sleep/wake. Its central clock
shows local time when available and clearly labels the pet-time fallback.
Back and close are icon-only controls at the bottom edge; rings enter and leave
from outside the panel. These navigation changes are part of the current slice.

# Presents collection and latched action

Presents shows individual gifts received from pets, with stable item identity,
origin pet, acquired time, and owner. Selecting one reserves it and pins its icon
at the top of the main screen. Switching pets preserves that selection. A clear
Give action transfers exactly that item to a different pet, commits ownership,
updates both relevant memories, and emits one readable event. Cancel unreserves
it. Busy/asleep/full-capacity failures preserve the item. Repeated input must not
duplicate it. Restart recovery must either restore a valid reservation or release
it safely. Define whether recipients can later return the same item before
implementing gift economy; do not silently convert provenance to a stack count.
A common latched-action model should also support future persistent activities.

# Nine collectible prizes and a shared catch interaction

Presents opens a 3×3 collection grid. Each slot represents one special prize type;
show its count and collection state, with individual provenance in the detail
view. Select a collected item to latch it for gifting. Closed presents pop open
with a bounded confetti/star burst; menu reward feedback may use a brief sparkle
state. Keep these effects in the existing fixed particle pool.

| Prize | Proposed discovery action | Catch presentation |
| --- | --- | --- |
| Butterfly | Occasional visitor after Going out in the garden | Gentle curved flight |
| Pearl Tooth | Complete three full brushing routines | A pearl bounces above the pet |
| Breakfast Sun | Enjoy breakfast on three different mornings | Small rising sun |
| Tea Sprite | Share three tea moments across distinct sessions | A sprite drifts with steam |
| Movie Star | Finish a movie moment with a comfortable pet | A star floats down |
| Bubble Gem | Complete a wash routine | A large slow bubble |
| Moon Charm | Wake after a completed scheduled sleep | A moon mote descends |
| Rainbow Seed | Visit the garden after caring for the pet | A seed rides a rainbow trail |
| Friendship Bow | Successfully give a collected present to another pet | A bow tumbles gently |

The exact frequency and counts above are proposed balance defaults. Store unlock
progress in each pet's memory. Use cooldowns, distinct-session/day requirements,
and capped rewards to avoid rewarding rapid repetition; medicine and shots never
award collectibles for repeated dosing. The brushing condition means three
completed routines, not three individual clicker taps.

All airborne prizes share one catch mechanic: a large visible touch target,
bounded lifetime, explicit eligibility, and no overlap with active menu targets.
On a successful catch, commit the owned item once and immediately transition to
the presents grid. Highlight the new slot and play its opening/confetti state.
Do not require an extra claim button. Repeated taps during that transition must
not duplicate the reward. A full collection stack needs an explicit policy
before spawning; closing or resetting must not lose an already caught item.

## Current slice boundary

The current implementation work is recorded in
[memo-018](../memos/memo-018-sleep-habits-and-collectible-presents.md).
Nine original 32x32 source PNGs are present in the asset manifest and HTML preview.
The bounded collection model keeps one owned item per type plus discovery history;
there are no stacks or item-detail provenance screens yet. Re-earning an owned
type does not duplicate it. Progress is per pet and persisted in save codec v3.

Prototype qualifications currently use three completed tea moments rather than a
session cooldown, three distinct morning days for breakfast, and a linked manual
sleep lasting at least six hours for the Moon Charm. The more elaborate flight
patterns and opening animation above remain polish proposals. Medicine and shots
must not unlock prizes, including indirect progress toward the Rainbow Seed.
Individual pet memory import/export remains separate future work.

# Minigame candidates and learning

Start with three tiny prototypes: catch a falling star (tap timing), echo a short
rhythm (two or three beats), and find a hidden present (three choices). Use bounded
rounds, fixed sprite pools, deterministic injected time, and a clear exit. Rewards
must have caps and variety bonuses; failure should be gentle. Record the activity
outcome in pet memory without storing an unbounded event log.

Separate care needs, short-term reactions, and slow learned preferences. Give each
pet authored baseline affinities and small learned offsets for activities and
time-of-day slots. Require varied interactions across days, cap daily changes,
and decay unused offsets toward the baseline. Add hysteresis to long-lived moods
and test simulated weeks plus rapid tapping. Current ID-based favorites are fixed
profiles, not learned preferences. Validate save migration and simulation timestep
invariance before enabling learning by default.

# Idle power management investigation

The exact Waveshare 1.75 board has a PCF85063 RTC powered through the AXP2101;
Waveshare documents battery-backed operation. The checked-in SDK/BSP dependency
configures CST9217 touch interrupt on GPIO11, active low. GPIO11 is a candidate
RTC-capable external wake input on ESP32-S3; verify the touch controller keeps
asserting it in its selected low-power mode and that its power rail remains on.
The ESP32 RTC timer also tracks time in sleep, with clock-source-dependent drift.
Do not confuse that timer with an already-set wall clock or the external RTC.

First proposal: namespaced idle thresholds, dim after inactivity, then light sleep
with touch wake. Quiesce LVGL/display transfers and sound before sleeping, drain
stale touch IRQs, and reconcile elapsed time on wake. Suppress sleep while USB
debug/capture or storage work is active. Test that wake consumes the first touch
rather than accidentally activating a menu. Preserve the selected brightness.
Measure total board current, wake latency, repeated wake reliability, and RTC
progress on battery. Deep sleep comes after verified checkpoints because it
reboots the application and loses normal RAM. No PMIC rail or charging changes
are part of this proposal. Auto-sleep is not enabled in the testing firmware.

# References

- [Architecture draft](rfc-001-virtual-pet-systems-architecture.md)
- [Current slice and release status](../memos/memo-017-release-downloads-and-expressive-slice.md)
- [ESP-IDF NVS documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html)
- [Board RTC and power hardware](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
- [ESP-IDF sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html)

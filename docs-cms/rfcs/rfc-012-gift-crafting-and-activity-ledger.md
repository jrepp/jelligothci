---
title: Gift crafting ledger and activity toys
status: Draft
author: Codex
created: 2026-10-20
tags: [activities, crafting, gifts, minigames, rfc]
id: rfc-012
project_id: jelligotchi
doc_uuid: 025eec59-4655-42c9-881d-5e954fee7e5d
---

# Summary

Expand the gift collection with simple two-gift recipes. Special crafted gifts
open new activities when used, including Jelli Chain, Bubble Lab, fishing and
Axoloty Crossing, plus collectible records for a jukebox activity. This is a design ledger for review, not implementation or approval of recipes and balance.
The owner requires an acquisition and recipe record for every gift; the tables
below cover the existing catalog and every proposed first-wave output.

The owner requested a swipe-controlled snake made of Jellis, won by reaching
seven Jellis long and then eating a star, and a simple falling-pair puzzle inspired
by Dr. Mario with three levels and a definite win. The owner also requested a
fishing minigame cartridge and a small Frogger-style cartridge starring Axoloty.
The owner also requested collectible records playing 30-second songs from small
PCM samples sequenced in real time, and a Gifts collection submenu making
cartridges and records visibly collectible.
Owner decisions after the [design review](../memos/memo-092-gift-gameplay-and-accessibility-review.md):
collectibles cannot be given away; every pet can play every owned collectible;
compact game checkpoints preserve progress; selected pet/collectible combinations
unlock special content through accumulated play time. Bubble Lab uses bottom-only
support, tap rotation, color matching to clear viruses, a top-out loss, and three
levels with increasing virus counts. Bubble Lab, Jelli Chain and jukebox show the
active pet cheering at lower left; songs provide distinct fun animations and
morale boosts. These decisions guide this draft; they do not implement or approve
the remaining recipes, thresholds, layouts, reward values or storage budgets. Reuse existing art wherever practical. Traveling and its airplane scene are a separate follow-on
increment; they do not require a crafted ticket under this proposal.

# Existing behavior and scope

[The prize catalog](../../content/prizes.json) is authoritative for implemented
gifts, keys and artwork. It already includes Teddy Bear beyond the original
collection described in [memo-018](../memos/memo-018-sleep-habits-and-collectible-presents.md).
This ledger intentionally records each gift because the owner explicitly requested
that inventory; acquisition thresholds remain linked to source rather than copied.
The snapshot was reviewed at `961eb62`. Update the ledger when catalog or recipe
changes land; later generate its acquisition and reverse-recipe views from content.

[Prize logic](../../core/prizes.c) owns progress, offers, catching and giving.
[Reward processing](../../core/pet_rewards.c) connects completed routines and
activities to offers, including sleep rewards. [Activity content](../../content/activities.json)
owns form, location, daily-reward and activity availability constraints.
The following describes current implementation, not the revised design: an offer is not ownership: the player must catch it. Offers are suppressed while
another offer is pending or the gift is already owned. Discovery persists after
Give, and the existing origin-pet restriction applies to Give. Existing
[pet discovery unlocks](../../content/pets.json) are separate from new toy activities.

# Gift ledger

Every row states acquisition, the recipe that creates it, and all recipes using
it. “None” means no recipe in this proposal, not an unresearched field. Recipe IDs
are local design identifiers, not allocated runtime IDs. Existing gifts retain
their current acquisition routes except Friendship Bow, whose Give dependency
is explicitly replaced below. Follow the linked functions for implemented eligibility,
thresholds, repeat acquisition and catch behavior.

## Existing gifts and proposed acquisition change

| Gift / catalog key | How acquired before catching the offer | Created by recipe | Used in recipes |
| --- | --- | --- | --- |
| Butterfly / `butterfly` | Chance from a completed garden outing; `garden` in [prize logic](../../core/prizes.c) | None | R05 |
| Pearl Tooth / `pearl-tooth` | Qualifying brush completions reach progress threshold; `jelli_prize_complete` in [prize logic](../../core/prizes.c) | None | R02, R07 |
| Breakfast Sun / `breakfast-sun` | Qualifying breakfasts on distinct eligible days reach progress threshold; `breakfast` in [prize logic](../../core/prizes.c) | None | R06, R09 |
| Tea Sprite / `tea-sprite` | Completed tea activities reach progress threshold; [prize logic](../../core/prizes.c) and [reward processing](../../core/pet_rewards.c) | None | R03, R09 |
| Movie Star / `movie-star` | Completed movie while awake with sufficient energy; [prize logic](../../core/prizes.c) | None | R01, R04, R11 |
| Bubble Gem / `bubble-gem` | Completed wash routine; [reward processing](../../core/pet_rewards.c) and [prize logic](../../core/prizes.c) | None | R02, R08 |
| Moon Charm / `moon-charm` | Wake after a qualifying recorded linked sleep session; `wake_rewards` in [reward processing](../../core/pet_rewards.c) | None | R04, R07, R10 |
| Rainbow Seed / `rainbow-seed` | Care sets pending progress; subsequent completed garden travel or outing offers it; [prize logic](../../core/prizes.c) | None | R03, R08, R10 |
| Friendship Bow / `friendship-bow` | Proposed: qualifying shared play with a pet, independent of owning a cartridge; exact activity/threshold pending. Replaces the implemented Give trigger in [prize logic](../../core/prizes.c) | None | R01, R05 |
| Teddy Bear / `teddy-bear` | Completed reward-eligible SNUGGLE under its form/location/daily restrictions; free play does not award it; [activity content](../../content/activities.json) and [reward processing](../../core/pet_rewards.c) | None | R06, R11 |

## Proposed new gifts and Play activities

These eleven gifts are the proposed shortlist, not approved implementation
commitments. The expanded shortlist requires a capacity review before implementation
(see Content, storage and validation boundaries). Each is acquired by explicitly
crafting its recipe, with ownership and discovery granted together. No minigame win or new random drop is required to
obtain these gifts. No output is an ingredient in this wave.

| Proposed gift / candidate key | How acquired / created by | Used in recipes | Proposed result of Play |
| --- | --- | --- | --- |
| Jelli Chain Cartridge / `jelli-chain-cartridge` | Craft R01 | None | Launch Jelli Chain snake minigame |
| Bubble Lab Cartridge / `bubble-lab-cartridge` | Craft R02 | None | Launch Bubble Lab falling-pair minigame |
| Teacup Garden / `teacup-garden` | Craft R03 | None | Play a tiny tending activity: water the cup, watch a sprout bloom |
| Pocket Planetarium / `pocket-planetarium` | Craft R04 | None | Play a quiet stargazing activity with drifting stars |
| Ribbon Kite / `ribbon-kite` | Craft R05 | None | Play a kite-flying activity with a bobbing ribbon |
| Picnic Kit / `picnic-kit` | Craft R06 | None | Play a teddy picnic activity with existing food and snuggle poses |
| Moonpond Fishing Cartridge / `moonpond-fishing-cartridge` | Craft R07 | None | Launch Moonpond Fishing timing minigame |
| Axoloty Crossing Cartridge / `axoloty-crossing-cartridge` | Craft R08 | None | Launch Axoloty Crossing river-crossing minigame |
| Morning Tea Record / `morning-tea-record` | Craft R09 | None | Open jukebox with its bright 30-second Morning Tea song |
| Moon Garden Record / `moon-garden-record` | Craft R10 | None | Open jukebox with its gentle 30-second Moon Garden song |
| Jelli Parade Record / `jelli-parade-record` | Craft R11 | None | Open jukebox with its playful 30-second Jelli Parade song |

## Proposed recipe ledger

Pairs are unordered: A + B and B + A resolve to the same unique output. Exactly
two distinct owned gift types are required; no self-pairs or three-item recipes.
All recipe ingredients must have acquisition routes independent of new crafts;
the replacement Friendship Bow route must work before the first cartridge.
There are no output-to-input cycles, and no activity requires its own reward to
be unlocked. Every ingredient and output appears in the gift ledger above.

| Recipe | Gift A | Gift B | Unique output | Design connection |
| --- | --- | --- | --- | --- |
| R01 | Friendship Bow | Movie Star | Jelli Chain Cartridge | Linked friends chase a star |
| R02 | Pearl Tooth | Bubble Gem | Bubble Lab Cartridge | Pearly pieces clear a bubbly puzzle |
| R03 | Tea Sprite | Rainbow Seed | Teacup Garden | A little garden grows inside a cup |
| R04 | Moon Charm | Movie Star | Pocket Planetarium | Moon and stars become a pocket sky |
| R05 | Butterfly | Friendship Bow | Ribbon Kite | A fluttering shape gains a flowing tail |
| R06 | Breakfast Sun | Teddy Bear | Picnic Kit | Breakfast becomes a picnic with a friend |
| R07 | Pearl Tooth | Moon Charm | Moonpond Fishing Cartridge | A pearly lure goes fishing under the moon |
| R08 | Bubble Gem | Rainbow Seed | Axoloty Crossing Cartridge | Pond bubbles and growing lily pads make a crossing |
| R09 | Breakfast Sun | Tea Sprite | Morning Tea Record | A sunny breakfast becomes a bright little tune |
| R10 | Moon Charm | Rainbow Seed | Moon Garden Record | A moonlit garden becomes a gentle lullaby |
| R11 | Movie Star | Teddy Bear | Jelli Parade Record | A teddy joins a cheerful show tune |

# Craft and Play

Owner decision: no collectible in this ledger can be given away or consumed by
Play. This includes original presents, crafted toys, cartridges and records.
Remove Give from this collection flow; do not route Play through the existing
reward-bearing Give command. Existing Give behavior described above is historical
implementation requiring an explicit migration, not a retained gameplay option.
Propose nonconsuming two-gift crafting: retain both ingredients, grant permanent
ownership/discovery of the result, reject an already-owned output, and never
create stacks or repeat craft rewards. Thus no craft/give/recraft loop exists.

Craft selects a compatible owned second gift, previews ingredients and output,
and requires explicit confirmation. Unsupported pairs show “No recipe.” Cancel
changes nothing. Revalidate ownership on confirmation and preserve pending catch
offers; grant crafted results directly without replacing those offers. Keep
existing provenance for migration/history without using it to restrict Play.

Every pet can play every owned collectible, regardless of origin, form or prior
play. Ownership exposes the activity/song to the whole player collection; first
Play does not create another ownership-like unlock state. Acquiring an item never
autoplays it. Play is the sole use action; Craft acquires missing items and Back
closes details. Activities and collection details lead to the same sessions.
Sleeping/busy pets prompt a clear return-to-care or resume path, without losing
ownership or restricting permanent access. Existing presents need a short,
nonconsuming play interaction specified before their migration ships.

## Pet-specific play discoveries

Owner requirement: some pet/collectible combinations produce special unlocks
through accumulated play time. These are optional bonuses, never prerequisites
for another pet to play the base collectible. Propose an authored eligibility
table with pet identity/form policy, collectible key, required active-play time,
unique output, and acquisition/recipe ledger entry for every resulting gift.
Exact pairs, thresholds and outputs remain pending; no unledgered reward ships.
Show progress and a hint on eligible details rather than hiding an unexplained
requirement. Decide whether eligibility follows an individual pet or a form before
allocating persistent counters; evolution must not silently erase earned time.

Count only foreground, unpaused gameplay or actual record playback; no offline,
menu, paused or stalled-frame accumulation. Assists, muted music and reduced
motion receive equal credit. Persist bounded accumulated time and one-time grants
atomically; switching pets, reloading or replaying a completion cannot duplicate
credit. Cap counters at their authored thresholds. Do not require wins or perfect
input for this time-based progression, and do not add idle grinding as a substitute
for enjoyable sessions.

# Gifts collection navigation

Gifts exposes Presents, Cartridges and Records (disks), with labeled icons and
permanent Collected/Total counts derived from content. Each gift belongs to one
category in one inventory. Paged cartridge and disk shelves show readable names
and motifs, with one primary action: Play if owned, Craft if ingredients are
available, otherwise See recipe. Ownership is permanent; there is no separate
“given away but playable” state or competing unlocked-media count.

Missing slots expose recipes and useful missing-ingredient acquisition hints.
Details show acquisition, ingredients, reverse recipes and optional pet-specific
play discovery progress. Keep that information off the shelf's primary cards.
A brief craft reveal connects the new item to its shelf and count, then offers
Play without autoplay. Back restores the previous page. Any optional persistent
New badge requires separately budgeted acknowledgment state.

Provide generous separated tap targets, explicit previous/next buttons alongside
swipe paging, visible keyboard focus, and non-hover desktop navigation. Keep the
primary action inside the round safe area. Prototype one card and details screen
at actual size before committing to badge, text or page density. Reuse cartridge
and disk silhouettes with distinct labels/motifs; never distinguish solely by color.

# Jukebox records and music

Owner requirement: records launch a jukebox activity with little 30-second songs,
using small PCM samples and real-time sequencing. Proposed first records are
Morning Tea (light plucks and soft percussion), Moon Garden (gentle bell tones)
and Jelli Parade (bouncy bass and taps). Compose original short arrangements;
share instrument samples across records while giving each song a distinct melody
and rhythm. These are song concepts, not approved compositions.

Owning a record makes its song playable by every pet in the jukebox; missing
records link to collection details. Play opens the selected song explicitly.
Play for 30 seconds including the ending envelope, then offer Replay, Choose
record and Exit without automatic looping. Show song name, progress and the
active pet at lower left, cheering/listening without covering controls. Each song
has a distinct fun animation: proposed Morning Tea gentle sways, Moon Garden
star-watching poses, and Jelli Parade a little march. Reuse existing poses and
music/celebration art from the [manifest](../../assets/slice/assets.json).

Owner requirement: songs grant morale boosts. The precise mapping to existing
needs/habits in [pet rewards](../../core/pet_rewards.c) and
[derived pet mood](../../core/personality.c), amount, eligibility window and cap require
source review and balance approval. Current mood is derived, not an independent
morale field; do not invent a new stat implicitly.
Propose a modest completion award with persistent per-pet eligibility/cap, shared
across records, so track switching, restart and reload cannot farm boosts. Partial
play and duplicate completion callbacks do not grant completion rewards. Muted,
assisted and reduced-motion playback remain equally eligible. Keep reward state
and completion atomic; exact cooldown/window and clock behavior remain pending.

Music respects master volume. Mute retains progress, completion and optional
beat/phrase visuals; these offer a visual experience without claiming equivalence
to hearing a song. Reduced motion replaces spinning/bouncing with static poses,
text and restrained progress changes. Pause freezes the sequence and visual time;
resume requires Ready and uses a short envelope to avoid clicks. Menus/focus loss
and care interruption pause safely. Propose replay from the beginning after a
process restart rather than saving audio voice state; already-earned play-time
credit survives and replay never duplicates a completion award. Explain this
record-specific restart behavior, distinct from game checkpoints.

## Proposed audio implementation boundaries

Current [sound rendering](../../core/sound.c) synthesizes short procedural cues;
its [public contract](../../include/jelli/sound.h) defines the shared mono PCM
format, output rate and bounded render block. [Audio asset guidance](../../assets/sound/README.md)
describes review exports, not an existing sampled-music player. The jukebox needs
a new bounded sampler/sequencer design; the current cue engine is not evidence
that music mixing or sample playback already exists.

Store a shared bank of short PCM instrument samples plus compact per-song event
sequences, not a rendered full-length recording per record. Each sample declares
format, frame count, root pitch and any validated loop points; each song declares
its events, tempo/timebase and explicit 30-second endpoint. Propose an initial
shared-bank target of at most 32 KiB and four simultaneous music voices, subject
to measured sound quality, flash/RAM and CPU review before adoption. Record actual
bank, sequence, voice-state and output-buffer costs separately.

Drive event scheduling from the audio sample cursor, not UI frame timing. Use
bounded fixed-point sample playback/pitch stepping, authored gain envelopes and
an adequately wide saturating mix before PCM output. Set explicit limits for
samples, voices, song events and events per render block; reject invalid content
at authoring time. Preload or use immutable addressable sample data; no filesystem
IO, allocation or blocking work in the render callback. Long frames must not
trigger an unbounded event catch-up loop. Test identical output across different
valid render-block partitions, including events inside a block and the endpoint.

Define music/care-cue priority and gain before integration so taps and pet voices
cannot unexpectedly replace a song or overflow the mixer. Keep platform audio
ownership in the existing [SDL](../../ports/sdl/sound_output.c) and
[ESP32](../../ports/esp32/main/sound_output.c) adapters. Desktop exports can verify
duration, clipping and deterministic timing; a firmware build and serial output
do not establish audible quality, underrun freedom or acceptable speaker volume.
Audition all three songs on desktop and physical hardware, and measure worst-case
render cost with care sounds before declaring playback ready.

# Minigame designs

## Jelli Chain

Owner requirement: the snake is a chain of Jellis, controlled by swipes. Reach
seven Jellis long, then eat a star to win. Reaching seven alone is not a win.

Proposed session: begin with a short chain on a bounded grid; each ordinary pickup
adds one Jelli. When length reaches seven, replace the ordinary pickup with one
star in a free reachable cell and stop further growth. Eating it completes the
session. Do not count the star as an eighth Jelli. The grid must leave a valid
route to the terminal star; use bounded free-cell selection, never an unbounded
random retry loop. Reachability must account for moving body, heading and
no-reversal rules; a free-cell flood fill alone is not proof. Prefer authored
guaranteed board/pickup patterns with bounded validation before runtime search.

Swipes choose cardinal heading; prohibit immediate reversal into the next segment
and accept at most one queued turn per movement step. Keyboard directions mirror
swipes on desktop. Contact with a wall or body ends the attempt with Retry and
Exit; retry resets the board, not ownership or unlocks. Pause freezes movement,
and resume discards stale gestures. Grid size, start length, speed and difficulty
remain proposed tuning to verify on the round display. Offer same-device large
tap-direction controls and relaxed speed; keep head/goal distinct under finger
occlusion. Show length out of seven, the next objective, a prominent score and
the active pet cheering at lower left, outside the board. Score rules are proposed
and must not penalize assists or gate collection access.

Reuse `creatures.baby-idle-a`, `creatures.baby-idle-b` and
`creatures.baby-happy` for the chain, `effects.orb` for pickups and `effects.star`
for the terminal star from the [asset manifest](../../assets/slice/assets.json).
Use integer sampling and a visibly distinct head; verify tiny sprites remain
recognizable rather than requiring a new creature sheet.

## Bubble Lab

Owner requirement: a simple Dr. Mario-style falling-pair game with exactly three
levels and a definite completion condition. Use original Jelli presentation and
existing art rather than reproducing another game's characters or interface.

Owner decisions: paired medicine pieces fall into a well of stationary viruses;
tap rotates, matching colors clear viruses, reaching the top is game over, and
exactly three levels increase the virus count. Pieces attach/support only below,
never from a neighbor at the side. Use original medicine/shot decorations at the
sides, a prominent score, and the active pet cheering at lower left. Reserve a
safe board area first: decorations and the pet cannot obscure cells or controls.

Propose left/right swipes plus large tap controls, a separate Rotate button as an
alternative to tapping the board, and downward swipe/Drop button for soft drop.
Keyboard actions mirror all controls. Offer relaxed fall speed and manual-drop
assistance with the same objectives and rewards. A first-level interactive example
teaches one clear, with persistent Help. Reject overlapping rotations/moves;
preview the resulting orientation so taps do not produce unexplained failures.

Proposed matching rule: a horizontal or vertical run of four matching colors
clears matching medicine and viruses. Give each color a redundant symbol. Viruses
stay fixed until cleared. The falling pair is controlled together before landing;
once locked, each half supports independently from below, with no lateral bond.
A surviving half after a clear falls vertically until supported from below or by
the floor. Resolve gravity and bounded cascades before spawning another pair.
Define top-out as a locked piece occupying the marked top danger row or an
obstructed spawn; finish any resulting clear/cascade first, then test victory
before top-out. Exact danger-row geometry needs a readable prototype.

Clear all viruses to finish each level; finish levels one through three to show
“Lab complete.” Retry restarts the current level, retaining previously completed
levels. Author a strictly increasing virus count and solvable patterns; exact
counts and board dimensions remain tuning. Provide several small authored variants
for replay, not random impossible wells. A bounded authored sequence must specify
exhaustion: propose a clearly announced Retry level when no further pieces remain,
never a silent stalled board. Test recovery after ordinary mistakes as well as a
perfect solution; reject sequences that make reasonable play routinely dead-end.
Checkpoint the board and level as described below.

Propose score from virus clears and cascades, with a readable breakdown and no
speed/assist penalty; exact values and any saved best score require approval and
storage budgeting. Reuse `effects.orb`, `prizes.bubble-gem`, `prizes.pearl-tooth`,
`effects.star` and happy poses from the [manifest](../../assets/slice/assets.json).
Reuse `health.medicine` and `health.shot` for side decorations. Use simple
virus silhouettes and redundant symbols; asset reuse must not make falling pieces and viruses
indistinguishable at actual size.


## Moonpond Fishing

Owner requirement: add a fishing minigame and its cartridge. Proposed session:
tap to cast into a small moonlit pond, wait for a bite, then tap while the bobber
is visibly submerged to reel in a fish. A ripple and bobber motion communicate
the bite without relying on color or sound. Space/Enter mirrors tap on desktop.
Early or late taps let the fish escape and return to Ready; there is no inventory
loss. One press produces one action, so a held button cannot cast and catch.

Proposed win: catch three fish in one session, then show “Catch complete” and
Replay/Exit. No overall countdown or limited bait in the first version. Each cast
has a bounded wait and one readable bite window; misses allow another cast.
Pause freezes both wait and bite timing, and resume discards stale presses.
Fish are session progress, not new collectible gifts or crafting ingredients.
Offer a persistent bite-until-confirm assist as well as a longer bite window;
early taps do not dismiss an assisted bite that has not happened yet. Keep catch
progress visible and present a small three-fish tableau with varied fish reactions,
so a catch has a payoff beyond a counter. Bounded short waits, anticipation and
reveal variety need replay testing, not progressively tighter reflex demands.
Timing and catch count remain tuning proposals for display and touch trials.

Reuse `effects.orb` for the bobber, existing ripple/celebration effects where
suitable, `prizes.moon-charm` for the pond motif and existing happy Jelli poses
from the [manifest](../../assets/slice/assets.json). Add only a small fish
silhouette and line if existing graphics cannot communicate the action clearly.

## Axoloty Crossing

Owner requirement: a small Frogger-style minigame cartridge starring Axoloty.
Propose an original pond crossing: swipe one cardinal step at a time to move
Axoloty from the near bank across lanes of drifting lily pads and logs to a
marked safe landing on the far bank. Desktop arrow keys mirror swipes. Keep
Axoloty's axolotl appearance; do not require the active care pet to be an axolotl.
The cartridge unlocks the minigame independently of pet-form discovery.

Proposed win: reach the far-bank landing once. Drifting platforms carry Axoloty;
missing a platform or drifting beyond the board returns to the near bank with
Retry/Exit, without losing gifts. Explain this as being swept back by the current,
not an axolotl drowning. Bank tiles are safe waiting places. Author a short,
repeatable lane schedule with reachable crossing windows; no random impossible
layouts, overall countdown or lives in the first version. Moving platforms still
create timing pressure: provide slower movement, large tap-direction alternatives
and an optional step-paced assist that advances the world deliberately with the
player, while preserving platform/support and goal rules. Prototype fairness of
that assist before adopting its exact cadence. Offer a few short authored lane
patterns for replay without increasing the required one-crossing win. Lane count, platform speeds
and input cadence remain proposals to verify on the round display.

Accept at most one queued step per movement interval, reject steps outside the
board and resolve support/carry/collision in a deterministic order. Pause freezes
platforms and input; resume discards stale gestures. Prevent a long frame or
resume from skipping across a hazard or awarding a false landing.

Reuse `creatures.axolotl-idle-1`, `creatures.axolotl-idle-2` and
`creatures.axolotl-happy` from the [manifest](../../assets/slice/assets.json),
with integer sampling and existing celebration effects. Add only simple platform
silhouettes and pond tiles where necessary. Distinguish safe platforms, water
and the goal by shape as well as color; preview Axoloty at the actual play size.

## Shared session behavior, checkpoints and access

A win gives a brief celebration; loss never removes a gift. No minigame win is
required to obtain the base cartridges. Optional accumulated-play discoveries and
jukebox morale are the explicit reward exceptions above; do not silently add
other care-stat rewards. Assists are available before and during a session without
inferior completion labels or collectible penalties.

Always expose Pause, Help and Exit through generous controls outside the board.
Pause freezes gameplay on menus, focus loss and care/sleep interruptions, discards
stale input, and resumes only after a deliberate Ready action with a proposed
short countdown that can be disabled in favor of a second confirmation. Explain
failures using text/icon cues, not sound/color alone. Keep decorative reduced
motion independent of game pace; remove cheering bounces and flashing clears
while retaining readable game-state changes.

Owner requirement: compact game checkpoints. Propose one resumable slot per game,
associated with its participating pet; switching pets must explicitly resume that
session or start a new one without silently overwriting it. Every pet remains free
to start any owned game. Menus retain exact in-memory state. Persist at safe action
boundaries, level completion and graceful Exit/interruption; explain Resume versus
Restart. A process crash may roll back to the last completed checkpoint, never
silently erase completed levels. Restart requires confirmation if progress exists.
Care time continues under existing clock rules; a care/sleep event pauses play and
saves a checkpoint before returning to care. No offline game movement or play-time
credit; exact interruption priority and checkpoint frequency require integration
review without inventing a way to freeze needs indefinitely.

Proposed packed payload targets (not measured save capacity): Jelli Chain at most
64 bytes for chain cells/heading/pickup/phase/score; Bubble Lab at most 192 bytes
for packed board/level/active pair/sequence cursor/score; fishing at most 32 bytes
for cast phase/catches/timing/variant; Axoloty at most 48 bytes for cell/platform
phase/layout. Total game payload target is 336 bytes, excluding headers, version,
pet association, integrity data and all play-time/reward counters. These are design
budgets to test, not permission to expand the existing save contract. Select board
sizes and packed encodings only after worst-case validation. Never serialize raw
pointers, audio voices or platform timer values. Store deterministic phase/remaining
time and versioned content identity; invalid or outdated checkpoints recover with
an explained safe restart while preserving collection and earned progression.

Use shape plus color, readable score/objective text, visible keyboard focus,
non-audio feedback and touch-only alternatives throughout. Test round-screen safe
areas, contrast, finger occlusion and target separation at actual size. Keyboard
controls do not establish blind access to visual grids; evaluate assistive/audio
navigation separately and document remaining limitations, without claiming an
accessibility conformance result from these design intentions.

## Fun and acquisition prototype gates

This is a gentle care-keepsake collection, not a deep consumable crafting economy.
Keep visible recipes and satisfying craft reveals; do not add grind to manufacture
length. Audit the fresh-player and established-save route to each game/song,
including irregular schedules, unavailable sleep linking and missed offers.
Propose a reliably approachable first toy and an alternate non-time-of-day path
for schedule-gated ingredients if trials show exclusion; exact alternate routes
need ledger entries and balance review, not silent changes to current rewards.
Friendship Bow's replacement shared-play route must be available without any new
craft or special pet-form prerequisite.

Before building the full unlock system, prototype one game, one shelf/details
card and one original 30-second record. Observe whether novices can explain the
objective, find Pause/Exit, correct a mistake, resume and voluntarily replay.
Report session lengths, mistaken inputs, retry causes and obscured cells, including
touch-only, keyboard-only, relaxed/manual modes, muted and reduced-motion trials.
Involve players with varied dexterity/vision where available. Test snake planning,
Bubble Lab's distinction between medicine and viruses, fishing anticipation,
Crossing route variety, and song recognition/replay appeal. Completion rates alone
are not evidence of fun. Audition hardware separately from desktop music exports.

Teacup Garden, Planetarium, Kite and Picnic remain backlog interaction concepts.
Specify their nonconsuming Play interactions before approval, reuse existing props
and poses, and do not imply new timed routines or reward sources.

# Content, storage and validation boundaries

Keep stable existing prize IDs and append new entries only after approval.
[prizes.h](../../include/jelli/prizes.h) owns catalog capacity, masks and provenance;
[save.c](../../core/save.c) and [save_tail.c](../../core/save_tail.c) own serialization
and legacy tails. Adding all eleven proposed gifts to the existing catalog exceeds
the current ownership-mask capacity; the earlier six-output shortlist filled it.
The expanded cartridge and record collection remains a proposal, not permission to widen
masks or reuse IDs. Before implementation, approve a smaller shipping subset or
a versioned capacity expansion covering ownership, discovery and provenance.
Neither choice establishes room for checkpoints, play-time counters or reward state in the bounded
[save contract](../../include/jelli/save.h).
Do not add backlog gifts by increasing a count without reviewing bit widths,
mask shifts, serialized layout, UI paging and worst-case checkpoint size.

Propose a small authored recipe table referencing prize keys and activity keys.
Validate unique unordered pairs, unique outputs, existing references, distinct
inputs, output-not-input, acyclic reachability, capacity and valid activity
bindings. Derive “used in” reverse links from that same table. An acquisition
record must explain each base gift's trigger or each crafted gift's creating
recipe; a catalog entry without an acquisition route fails authoring validation.
The ledger should expose both directions without duplicating mutable tuning.

Crafting, discovery, checkpoint and reward updates must be atomic and idempotent.
Version the migration: preserve existing ownership/discovery/provenance; do not
turn historically discovered-but-given-away gifts into owned items without an
explicit migration decision. Owned collectibles become playable by every pet;
no first-Use unlock bit is required. Start new special-play counters and rewards
at zero, never infer historical play. Preserve unsupported saves rather than
truncating them. Measure worst-case game state, encoded bytes, counter cardinality
and workspaces including migration overlap before selecting runtime bounds.
Follow [engine/host contracts](../../docs/development.md) and the
[bounded-memory proposal](rfc-009-bounded-memory-systems-and-adoption.md); no IO,
heap allocation or unbounded searches in gameplay update/render paths.

# Incremental adoption and acceptance

1. Prototype one game, one collection card and one original track; record usability,
   fun and hardware audio evidence before approving the full system.
2. Approve the shipping subset, unique recipes, replacement Bow acquisition,
   migration, checkpoint encodings and reward/time-counter budgets.
3. Implement bounded content validation and collection/Craft/Play with one inert
   fixture, then Jelli Chain and its seven-Jelli/star finish.
4. Deliver Bubble Lab's three levels with increased virus counts and checkpoints.
5. Deliver fishing and Axoloty separately after touch/assist/readability trials.
6. Deliver jukebox with distinct animations and the approved morale policy; add
   optional pet/play-time discoveries only with complete ledger entries and bounds.
7. Keep the other activity toys in backlog pending interaction approval.

Acceptance for implementation:

- Every gift has acquisition, creation and complete reverse recipe references;
  eleven proposed recipes have eleven distinct unordered pairs and outputs.
  Invalid references, duplicates, cycles and unsupported capacity fail validation.
- Craft cancellation/failure changes nothing; repeat confirmation/catch/reload
  cannot duplicate gifts or rewards. Preserve pending offers and historical saves.
- No collection action invokes Give. Every pet can Play every owned collectible;
  duplicate crafting fails and shelf counts never fall through ordinary play.
- Friendship Bow can be acquired without giving or owning a new cartridge. Audit
  new-player/schedule-limited paths to the first toy and record.
- Snake wins only when a seven-Jelli chain eats its star. Test legal moving-body
  reachability, turn queue, collisions, score display, pet placement and checkpoints.
- Bubble Lab clears matching colors with redundant symbols, uses bottom-only
  support, handles halves/cascades/top-out/sequence exhaustion, and wins only after
  three increasingly populated virus levels. Test checkpoints and mistake recovery.
- Fishing requires its catch target; verify normal timing and persistent-bite
  assistance, no held-input double action, cast recovery and checkpoints.
- Crossing wins only at the goal; verify carry/support/order, slow/step assistance,
  reachable patterns, stalled frames and checkpoint restoration.
- Pause/Ready/Resume/Restart/Exit, sleep/focus interruption and process restart
  have explicit tested outcomes. No offline movement or accumulated-play credit.
- Song duration remains 30 seconds including envelopes. Test mute, pause, replay,
  distinct animations, optional visual phrases, cue overlap, bounded mixing and
  sample-clock timing; independently audition and measure device playback.
- Morale and special-play awards obey approved eligibility/caps across track or
  pet switches, reloads and repeated callbacks. Assists/mute receive equal credit;
  time counters saturate and never accrue during paused/offline/stalled time.
- Validate checkpoint formats, worst-case bytes, content-version recovery and
  migration separately from runtime RAM. No proposed byte target is assumed to fit.
- Use deterministic injected input/time, core tests and sanitizers, desktop visual
  inspection, and separate physical touch/readability/audio evidence. Complete
  the fun/accessibility prototype gates above before calling the design validated.

# Tradeoffs and open decisions

Owner decisions are permanent collectible ownership without Give, universal pet
access, compact game checkpoints, optional pet/collectible play-time discoveries,
Bubble Lab's clarified rules/presentation and song-specific animations/morale.
These supersede the earlier Give/recraft and first-Use unlock proposals. The RFC
remains Draft; none of these documentation changes implement game behavior.

Still proposed: nonconsuming recipe details (including revised R11), exact Bow
shared-play acquisition, shipping subset, base-present Play interactions, any
alternate schedule-friendly acquisition, special discovery pairs/outputs/time
thresholds and pet identity policy, morale mapping/value/eligibility/caps, scores,
board dimensions/timings, assist cadence, checkpoint frequency/encoding/budget,
care interruption ordering, old discovered-only gift migration, sample-bank/voice
budgets, arrangements and cue mixing. Validate these with source review, packed
save measurements and playtests. New tiers, hidden recipes and travel souvenirs
remain backlog; no unledgered gift or airplane-unlock recipe is introduced.

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
Axoloty Crossing. This is a design ledger for review, not implementation or approval of recipes and balance.
The owner requires an acquisition and recipe record for every gift; the tables
below cover the existing catalog and every proposed first-wave output.

The owner requested a swipe-controlled snake made of Jellis, won by reaching
seven Jellis long and then eating a star, and a simple falling-pair puzzle inspired
by Dr. Mario with three levels and a definite win. The owner also requested a
fishing minigame cartridge and a small Frogger-style cartridge starring Axoloty.
Names, ingredients, fishing/crossing win conditions, tuning and persistence
policies below are proposals. Reuse existing art wherever practical. Traveling and its airplane scene are a separate follow-on
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
An offer is not ownership: the player must catch it. Offers are suppressed while
another offer is pending or the gift is already owned. Discovery persists after
Give, and the existing origin-pet restriction applies to Give. Existing
[pet discovery unlocks](../../content/pets.json) are separate from new toy activities.

# Gift ledger

Every row states acquisition, the recipe that creates it, and all recipes using
it. “None” means no recipe in this proposal, not an unresearched field. Recipe IDs
are local design identifiers, not allocated runtime IDs. Existing gifts retain
their current acquisition routes. Follow the linked functions for eligibility,
thresholds, repeat acquisition and catch behavior.

## Existing gifts

| Gift / catalog key | How acquired before catching the offer | Created by recipe | Used in recipes |
| --- | --- | --- | --- |
| Butterfly / `butterfly` | Chance from a completed garden outing; `garden` in [prize logic](../../core/prizes.c) | None | R05 |
| Pearl Tooth / `pearl-tooth` | Qualifying brush completions reach progress threshold; `jelli_prize_complete` in [prize logic](../../core/prizes.c) | None | R02, R07 |
| Breakfast Sun / `breakfast-sun` | Qualifying breakfasts on distinct eligible days reach progress threshold; `breakfast` in [prize logic](../../core/prizes.c) | None | R06 |
| Tea Sprite / `tea-sprite` | Completed tea activities reach progress threshold; [prize logic](../../core/prizes.c) and [reward processing](../../core/pet_rewards.c) | None | R03 |
| Movie Star / `movie-star` | Completed movie while awake with sufficient energy; [prize logic](../../core/prizes.c) | None | R01, R04 |
| Bubble Gem / `bubble-gem` | Completed wash routine; [reward processing](../../core/pet_rewards.c) and [prize logic](../../core/prizes.c) | None | R02, R08 |
| Moon Charm / `moon-charm` | Wake after a qualifying recorded linked sleep session; `wake_rewards` in [reward processing](../../core/pet_rewards.c) | None | R04, R07 |
| Rainbow Seed / `rainbow-seed` | Care sets pending progress; subsequent completed garden travel or outing offers it; [prize logic](../../core/prizes.c) | None | R03, R08 |
| Friendship Bow / `friendship-bow` | Successfully Give another gift to an eligible pet; giving a bow cannot replace itself; [prize logic](../../core/prizes.c) | None | R01, R05 |
| Teddy Bear / `teddy-bear` | Completed reward-eligible SNUGGLE under its form/location/daily restrictions; free play does not award it; [activity content](../../content/activities.json) and [reward processing](../../core/pet_rewards.c) | None | R06 |

## Proposed new gifts and use unlocks

These eight gifts are the proposed shortlist, not approved implementation
commitments. The expanded shortlist requires a capacity review before implementation
(see Content, storage and validation boundaries). Each is acquired by explicitly
crafting its recipe, with ownership and discovery granted together. No minigame win or new random drop is required to
obtain these gifts. No output is an ingredient in this wave.

| Proposed gift / candidate key | How acquired / created by | Used in recipes | Proposed result of Use |
| --- | --- | --- | --- |
| Jelli Chain Cartridge / `jelli-chain-cartridge` | Craft R01 | None | Unlock and launch Jelli Chain snake minigame |
| Bubble Lab Cartridge / `bubble-lab-cartridge` | Craft R02 | None | Unlock and launch Bubble Lab falling-pair minigame |
| Teacup Garden / `teacup-garden` | Craft R03 | None | Unlock a tiny tending activity: water the cup, watch a sprout bloom |
| Pocket Planetarium / `pocket-planetarium` | Craft R04 | None | Unlock a quiet stargazing activity with drifting stars |
| Ribbon Kite / `ribbon-kite` | Craft R05 | None | Unlock a kite-flying activity with a bobbing ribbon |
| Picnic Kit / `picnic-kit` | Craft R06 | None | Unlock a teddy picnic activity with existing food and snuggle poses |
| Moonpond Fishing Cartridge / `moonpond-fishing-cartridge` | Craft R07 | None | Unlock and launch Moonpond Fishing timing minigame |
| Axoloty Crossing Cartridge / `axoloty-crossing-cartridge` | Craft R08 | None | Unlock and launch Axoloty Crossing river-crossing minigame |

## Proposed recipe ledger

Pairs are unordered: A + B and B + A resolve to the same unique output. Exactly
two distinct owned gift types are required; no self-pairs or three-item recipes.
All recipe ingredients have existing acquisition routes independent of new crafts.
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

# Craft, Use and Give

Proposed first-wave policy: crafting checks ownership of both ingredients but
keeps them, and outputs are reusable. This avoids introducing item quantities or
forcing players to lose hard-earned gifts before trying a toy. Ingredient
consumption is an explicit owner decision, not implied by “combine.” Reject a
craft when the output is already owned; do not create stacks or extra rewards.
If an output is later given away, its recipe can recreate it while both
ingredients are owned. Recipe discovery and activity unlocks grant no repeat
rewards from this loop.

Present details offer distinct Craft, Use, Give and Put away actions as applicable.
Craft first selects a compatible second owned gift, shows both ingredients and
the result, and commits only on explicit confirmation. Unsupported pairs show
“No recipe” without state changes. Cancel retains inventory. Revalidate ownership
at commit; preserve an existing pending catch offer. Proposed crafting grants the
result directly rather than replacing that offer. Record origin as the active
crafter's pet ID, preserving the existing prohibition on giving to the origin pet.

Acquiring or crafting a toy does not launch gameplay. The first successful Use
records a durable activity unlock and launches that activity. Proposed unlocks
are player-wide and remain after Give; later launches are available in Activities
without owning the toy. Repeat Use launches again without duplicate unlocks.
Reject Use while asleep or busy without recording an unlock. Put away never
consumes a gift. Give retains current transfer/consumption semantics; it does not
implicitly Use the gift or start an activity. Detail text must explain these
consequences before either action.

# Minigame designs

## Jelli Chain

Owner requirement: the snake is a chain of Jellis, controlled by swipes. Reach
seven Jellis long, then eat a star to win. Reaching seven alone is not a win.

Proposed session: begin with a short chain on a bounded grid; each ordinary pickup
adds one Jelli. When length reaches seven, replace the ordinary pickup with one
star in a free reachable cell and stop further growth. Eating it completes the
session. Do not count the star as an eighth Jelli. The grid must leave a valid
route to the terminal star; use bounded free-cell selection, never an unbounded
random retry loop.

Swipes choose cardinal heading; prohibit immediate reversal into the next segment
and accept at most one queued turn per movement step. Keyboard directions mirror
swipes on desktop. Contact with a wall or body ends the attempt with Retry and
Exit; retry resets the board, not ownership or unlocks. Pause freezes movement,
and resume discards stale gestures. Grid size, start length, speed and difficulty
remain proposed tuning to verify on the round display.

Reuse `creatures.baby-idle-a`, `creatures.baby-idle-b` and
`creatures.baby-happy` for the chain, `effects.orb` for pickups and `effects.star`
for the terminal star from the [asset manifest](../../assets/slice/assets.json).
Use integer sampling and a visibly distinct head; verify tiny sprites remain
recognizable rather than requiring a new creature sheet.

## Bubble Lab

Owner requirement: a simple Dr. Mario-style falling-pair game with exactly three
levels and a definite completion condition. Use original Jelli presentation and
existing art rather than reproducing another game's characters or interface.

Proposed rules: paired bubble pieces fall into a small well containing stationary
bubble targets. Left/right swipes move the pair, tap rotates, and downward swipe
soft-drops. Desktop keys provide equivalent actions. A horizontal or vertical run
of four matching symbols clears matching pieces and targets. Unsupported pieces
fall; resolve bounded cascades before spawning another pair. Targets stay fixed
until cleared. Reject rotations/moves that overlap the wall or occupied cells.
A blocked spawn ends the attempt; Retry restarts the current level.

Clear every target to finish a level. Finish levels one, two and three in order
to win the whole game, then show a clear “Lab complete” result and Replay/Exit.
Proposed progression: level one teaches a single pattern, level two introduces a
second pattern and offset targets, and level three combines three patterns and
more targets. Author small solvable layouts and a bounded piece sequence per
level; do not rely on random luck to make a level completable. Exact board size,
target counts and fall timing need touch/readability trials. Three levels are a
requirement; these layouts and matching rules are proposed choices.

Reuse `effects.orb`, `prizes.bubble-gem`, `prizes.pearl-tooth`, `effects.star`
and existing happy Jelli poses from the [manifest](../../assets/slice/assets.json).
Distinguish piece types with symbols as well as colors. Palette variants may help
reuse art, but the minigame must remain readable without color discrimination;
the character palette authoring work is a separate increment.

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
layouts, timer pressure or lives in the first version. Lane count, platform speeds
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

## Shared session behavior and rewards

Propose no new gift drops or permanent stat rewards for the first playable
minigames. A win gives a short existing celebration; deciding any progression
reward is separate balance work. Explicit Exit always returns to pet care, and
loss never removes a gift. Pause on menus/focus loss. Do not progress a board in
offline catch-up. Initially save unlocks only; interruption discards a transient
attempt and explains that restarting begins a new attempt. Review care-time
progression and sleep interruption before implementation so games cannot silently
bypass existing activity or clock rules.

Other proposed activities should reuse the cup, seed, moon, star, butterfly,
bow, teddy and food assets in the [manifest](../../assets/slice/assets.json), adding
only indispensable combined-gift silhouettes or small props. Reuse pose sequences
and celebration effects. These are unlock concepts, not approved timed routines
or new reward sources.

# Content, storage and validation boundaries

Keep stable existing prize IDs and append new entries only after approval.
[prizes.h](../../include/jelli/prizes.h) owns catalog capacity, masks and provenance;
[save.c](../../core/save.c) and [save_tail.c](../../core/save_tail.c) own serialization
and legacy tails. Adding all eight proposed gifts to the existing catalog exceeds
the current ownership-mask capacity; the earlier six-output shortlist filled it.
Fishing and Axoloty Crossing remain recorded proposals, not permission to widen
masks or reuse IDs. Before implementation, approve a smaller shipping subset or
a versioned capacity expansion covering ownership, discovery and provenance.
Neither choice establishes room for new unlock state in the bounded
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

Crafting, discovery, provenance and unlock updates must checkpoint atomically and
be idempotent under repeated input. New unlock state requires explicit versioning
and migration: preserve old gift ownership/discovery and origin IDs, start new
activity unlocks absent, and never infer a Use from historical discovery. Preserve
unsupported saves rather than truncating them. Measure worst-case game state,
encoded bytes and minigame workspace before selecting fixed runtime bounds.
Follow [engine/host contracts](../../docs/development.md) and the
[bounded-memory proposal](rfc-009-bounded-memory-systems-and-adoption.md); no IO,
heap allocation or unbounded searches in the gameplay update/render path.

# Incremental adoption and acceptance

1. Review this ledger: choose the recipes, ingredient consumption, unlock scope,
   Give behavior and minigame rules. This document changes no gameplay defaults.
2. Implement the approved bounded recipe/acquisition catalog, generated ledger
   checks and save migration; add Craft/Use UI with one inert toy fixture first.
3. Deliver Jelli Chain with the required seven-Jelli/star win, reusing art.
4. Deliver Bubble Lab with three solvable levels and explicit final victory.
5. Deliver Moonpond Fishing and Axoloty Crossing separately after approving their
   rules and resolving gift capacity; reuse art and verify touch controls.
6. Add the other shortlisted activities separately after their interactions are
   approved; treat further gift ideas as backlog until capacity is addressed.

Acceptance for implementation:

- Every gift has an acquisition route, creating recipe or explicit None, and
  complete reverse recipe references; every recipe links exactly two ingredients
  to one output. Validate consistency automatically against authored content.
- Reversed ingredient order works identically; invalid, missing, duplicate and
  cyclic recipes fail validation. Failed or canceled crafts change no state.
- Repeated confirmation, Use, reload and catch cannot duplicate rewards or lose
  provenance; pending offers survive crafting. Old saves preserve existing gifts.
- Give, Use, Craft and Put away remain separate, with clear asleep/busy behavior.
- Snake cannot win at six, or merely by reaching seven; it wins only after the
  seven-Jelli chain eats its star. Test turns, collisions, pause, retry and Exit.
- Bubble Lab wins a level only when every target is gone and wins the game only
  after all three levels. Test rotation rejection, clears, cascades, blocked
  spawns, restart and authored solvability.
- Fishing wins only after the proposed catch target; early/late taps do not
  increment it. Test repeated input, bounded bite windows, pause and retry.
- Axoloty Crossing wins only on the goal landing; water/current failures and
  off-board moves cannot win. Test platform carry, lane reachability, pause,
  stalled frames and retry.
- All four games use bounded workspaces and deterministic injected input/time; test
  core behavior and sanitizers, inspect desktop rendering, and separately verify
  physical swipe/tap controls, readability and timing on the target display.

# Tradeoffs and open decisions

Nonconsuming recipes make experimentation gentle but shorten collection pacing.
Consuming recipes would need deliberate reacquisition and confirmation policy;
stacked inventory is outside this increment. Permanent Use unlocks allow a toy to
be given away safely, but need saved unlock state and an Activities entry point.
Ownership-gated replay would be simpler storage-wise but could remove access after
Give. Both alternatives require an explicit owner choice.

Still open: recipe/name approval, which shortlist activities ship first, quantity
or consumption semantics, player-wide versus pet-specific unlocks, crafted origin
rules, whether crafted Give should earn a Friendship Bow under existing rules,
care-time interaction, board sizes, timing, match rules and rewards. Preview these
choices before treating them as production balance. Additional crafted tiers,
recipe discovery puzzles, rewards for wins and travel souvenirs remain backlog;
no unledgered gifts or implied airplane-unlock recipe are introduced here.
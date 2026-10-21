---
title: Gift collection gameplay and accessibility review
author: Codex
created: 2026-10-20
tags: [accessibility, gameplay, gifts, memo, review]
id: memo-092
project_id: jelligotchi
doc_uuid: 98b63f3f-1743-4143-8fc8-9b9b4ea34e12
---

# Verdict and evidence

Review of [RFC-012](../rfcs/rfc-012-gift-crafting-and-activity-ledger.md) at
`64902e3`, with independent gameplay and accessibility reviewers. The concept has
appeal, but is not ready for full implementation. The strongest idea is turning
care-earned gifts into tangible cartridges and records. The weakest part is the
assumption that a recognizable game, a win condition and unlimited retries will
produce a satisfying, accessible experience on a small round display.

This is a design review, not playtest evidence. Findings below separate definite
rule conflicts from predicted experience risks. Recommendations remain unapproved;
this memo does not change recipes, rewards or gameplay defaults.

# Findings in priority order

## High: two recipes contradict the unique-pair contract

R01 and R11 both use Friendship Bow + Movie Star, but produce Jelli Chain
Cartridge and Jelli Parade Record respectively. An unordered-pair lookup cannot
choose both. A read-only table check found eleven recipes but only ten unique
pairs. Change one ingredient pair before implementation; for example, Movie Star
+ Teddy Bear is currently unused, though the recipe and resulting acquisition
pacing still need review. Validate pair uniqueness and reciprocal ledger links
when editing proposals, not just when generating runtime content.

## High: nonconsuming crafts plus Give create a repeat reward loop

The Craft, Use and Give section permits free recreation after Give and preserves
existing Give semantics. [jelli_prize_gift](../../core/prizes.c) grants social and
bond gains and may progress the Friendship Bow reward. Craft on pet A, give to
eligible pet B, then repeat: the origin restriction does not stop this loop.
The proposal only excludes repeated discovery/unlock rewards, leaving these care
rewards intact. Recommend no care/bond/Bow rewards for giving freely recraftable
outputs, or a separately specified bounded reward policy. Do not “fix” this by
silently consuming hard-earned ingredients. Test the entire craft/give/recraft
cycle across pets and reloads after a policy is approved.

## High: motor access is missing from the game rules

Jelli Chain and Axoloty require timed directional swipes on hardware. Desktop
arrows are not an alternative for a player using only the device. Bubble Lab adds
swipe/tap ambiguity; fishing still requires a timed reaction despite unlimited
attempts. Missing contracts: same-device non-swipe controls, adjustable pace,
forgiving input windows, usable Pause/Exit, and safe resumption.

Recommend large tap-direction controls alongside swipes, a relaxed pace setting,
Bubble Lab manual-drop assistance, fishing's persistent bite-until-confirm option,
and a deliberate ready/resume step. Do not require rapid taps or precise drag
paths. Each assist should preserve the game's objective without withholding
collection access or marking completion as inferior. Prototype whether controls
fit without covering the board before committing to layouts.

## High: a gentle virtual pet can still punish interruptions

Shared session behavior saves unlocks only and discards interrupted attempts;
Bubble Lab requires three successive levels. A player interrupted near the end
may lose the longest session, while care and sleep timing are still undefined.
Specify what menus, focus loss, sleep, Exit and restart each preserve. Recommend
at least retaining the completed level boundary for Bubble Lab, subject to save
budget approval. Decide care progression before balancing session lengths; an
activity should not become either a neglect trap or a way to freeze needs forever.

## Medium: two games still have incomplete correctness rules

Jelli Chain's reachable-star promise needs a moving-snake definition: a connected
empty cell is not necessarily reachable without reversal or collision as the
body moves. Use guaranteed layouts or bounded snake-state reasoning, not a plain
empty-cell flood fill as proof. Bubble Lab must define surviving half-pieces,
attachment/support behavior and what happens when its bounded piece sequence
runs out. A sequence solvable under perfect play may become unwinnable after an
ordinary mistake. Specify recovery or an explicit retry condition before tuning.

## Medium: crafting is a checklist, not yet a compelling collection loop

Recipes are fully visible, ingredients reusable, outputs never ingredients and
play permanently unlocked. This is a reasonable gentle unlock system, but offers
few decisions once the base gifts are collected. Existing players may craft the
whole affordable set in one visit; new players may wait on sleep, multi-day or
social acquisition routes before seeing a game. Do not promise crafting depth
that these rules do not provide.

Keep the nonconsuming approach for now. Map the earliest plausible acquisition
path for each cartridge and record from a fresh pet and an established save.
Include irregular schedules, unavailable sleep linking and missed catch offers
in that audit; retained acquisition rules can still exclude players from new
content. Choose one reliably approachable first toy, show useful missing-ingredient hints,
and make the craft reveal rewarding. Validate pacing before adding currencies,
recipe tiers or new grind merely to extend playtime.

## Medium: collection states make a simple toy library hard to understand

The proposed shelf combines owned, discovered, craftable and permanently unlocked
states, plus current-ownership counts. After Give, a game remains playable but the
shelf offers Play only on owned entries; the player must know to use Activities.
That is needless friction in the collection's main purpose.

Recommend Play on every unlocked cartridge/record detail, with separate “Not
currently owned” text when applicable. Use a primary progress count based on
discovery or unlocked media and show ownership in details, clearly labeled.
Keep one primary action per card: Play, Craft, or See recipe. Detail screens can
hold the full acquisition/provenance information. Avoid a dense badge legend.

## Medium: the games have endpoints but weak replay specifications

| Experience | Assessment | Recommended prototype question |
| --- | --- | --- |
| Jelli Chain | Strongest short-session hook: seven Jellis then a star is clear and distinctive | Can players read the head and plan a turn before their finger obscures the next cell? |
| Bubble Lab | Most strategic potential, but highest rules and screen-density risk | Can a novice distinguish fixed targets from falling pieces and explain one clear without coaching? |
| Fishing | As written, mostly waiting followed by a reflex test, repeated three times | Do anticipation, bite feedback and varied fish personalities make a second session appealing without adding grind? |
| Axoloty Crossing | Strong character fit, but one repeatable route may be exhausted after one success | Are a few short authored lane patterns enjoyable to revisit, without increasing the required win length? |
| Jukebox | A good cozy collectible payoff; quality depends on the actual music | Does a listener voluntarily replay a 30-second tune and recognize its record from its sound and sleeve? |

Do not confuse predictable solvability with replay variety. A small selection of
authored patterns can retain fairness without becoming one memorized solution.
Conversely, do not add rewards or daily chores just to make players repeat a weak
game. Fun needs observation, not an assumption that familiar genres are enough.

## Medium: visual and sensory accessibility remain unverified

Symbols plus colors and mute support are good foundations. They do not establish
readability after shrinking reused sprites. Round-screen clipping, finger
occlusion, contrast, text size and focus visibility need actual-size prototypes.
Add reduced decorative motion for spinning records/celebrations, persistent
instructions and non-audio success/failure cues. Muted progress makes jukebox
controls operable but does not make a musical experience equivalent for a deaf
player; propose optional beat/phrase visuals without making them another timing
test. Blind access to a visual grid is not solved by keyboard bindings; document
that limitation and evaluate meaningful audio/assistive navigation separately.

## Medium: scope is ahead of evidence

The proposal introduces four games, three records, four activity toys, collection
navigation, recipe logic, save migration and a new sampler/sequencer. The full
catalog exceeds the current [prize storage contract](../../include/jelli/prizes.h).
Asset reuse saves drawing work, not interaction design or validation. A proposed
32 KiB bank and four voices are targets, not measured proof of sound quality or
runtime headroom.

Prototype one game, one shelf card and one original 30-second track before
building all unlock plumbing. Preserve the rest of the ledger as backlog.
After usability and audio evidence, choose the shipping subset and migration
scope. Do not treat the current adoption list as approval to implement everything.

# What to preserve

Keep explicit Use instead of autoplay on acquisition, visible recipes,
nonconsuming ingredients, unlimited retries, separate Give, no forced reward
farming, symbol-plus-color differentiation, original art/music and short sessions.
The seven-Jelli/star ending, Axoloty identity and tangible media collection give
the proposal personality. Improve how those ideas feel before adding more systems.

# Validation before design approval

1. Resolve the duplicate pair, crafted-Give rewards and retained Play access.
2. Walk through new-player and established-player acquisition, craft, play, Give
   and replay paths; check no hidden ownership or pet-form prerequisites appear.
3. Prototype controls at actual device size with a novice and players with varied
   dexterity/vision where available. Record mistaken inputs, obscured cells,
   understanding of failure and time needed to find Pause/Exit. Include touch-only,
   keyboard-only, slower pace, reduced motion and muted trials.
4. Observe whether players voluntarily replay without being prompted or rewarded;
   ask what they thought their next action would do. Report session length and
   retry causes rather than declaring “fun” from completion rate alone.
5. Audition the same 30-second track on desktop and the device speaker; measure
   mix cost, memory and underruns separately from perceived musical quality.

No playtests, accessibility conformance audit or device audio measurements were
performed for this review. Recipe-conflict detection and source inspection support
the concrete defects; experience predictions require the trials above.

# Accessibility references

[W3C pointer gesture guidance](https://www.w3.org/WAI/WCAG22/Understanding/pointer-gestures.html)
supports providing simple pointer alternatives to path-based gestures.
[Target size guidance](https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum.html)
explains size/spacing barriers; its CSS-pixel criteria are web criteria, not a
physical-device millimeter specification.
[Timing guidance](https://www.w3.org/WAI/WCAG22/Understanding/timing-adjustable.html)
explains why adjustment or alternatives matter and includes exceptions.
These are design references, not a finding that every game timer violates WCAG
or that this native device has passed a web compliance standard.

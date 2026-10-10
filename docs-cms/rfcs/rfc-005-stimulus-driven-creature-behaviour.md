---
title: Stimulus-driven creature behaviour, activities as data, and the magic-number scrub
status: Draft
author: Jacob Repp
created: 2026-10-10T06:40:00Z
tags: [behaviour, content, creatures, activities, data-driven]
id: rfc-005
project_id: jelligotchi
doc_uuid: f56747f9-66dd-4b2b-85ea-5d8a190352df
---

# Summary

Creatures react to **stimuli**: presents, environment, activities, needs, touch
and idle time. They enter authored **behaviour states**, such as curious,
studying, contemplating and asking for help, that are defined in content data.
The C core becomes a generic, bounded engine: it turns game events into
stimuli, matches them against the active species' **repertoire**, and runs the
chosen state's effects, duration, cooldown and optional **request**.
Activities (moments) also move into data, and **reading** with a book is added.
BUBBLE, the curious, studious axolotl, gets the first repertoire. Creature-
and content-specific magic numbers move out of C into validated data.

# Motivation

The user wants creatures with "more complex behaviors and reactions to presents,
environments and activities", so that creatures like BUBBLE can be curious or have
extra states such as studying, contemplating and asking for help. The C code should
be "the behavioral code … driven by stimulus and data". Magic numbers should be
scrubbed "as we are going to be importing more creatures". Separately, the user
wants a reading activity that BUBBLE enjoys.

RFC-001 already sketches a behaviour profile: priority rules, cooldowns,
preference weights, dwell times and a seeded PRNG. RFC-002 calls for authored
affinities. ADR-012 made presentation data-driven (clips, pose rules, scales);
this RFC covers the missing half, autonomous behaviour.

The user's decisions (2026-10-10):

| Question | Decision |
| --- | --- |
| Gameplay weight | Light effects plus requests: small capped need and bond changes; answering a request gives a bond bonus, and ignoring it simply times out, with no penalty |
| Persistence | Save only the current state, its deadline and its cooldown. Offline catch-up expires states and never starts new ones |
| Art | Reuse existing frames now, with captions and effect sprites; drawn clips slot in later through Jelli Art |
| Scope | BUBBLE first. Mint and Lilac keep today's behaviour through an empty repertoire |

# Detailed Design

## Stimuli (C vocabulary, fixed)

| Stimulus | Raised when | Value |
| --- | --- | --- |
| `idle` | each behaviour second while awake, idle and stateless | — |
| `present_caught` | a prize is caught | prize index |
| `present_given` | a gift is given to the pet | prize index, or 255 for a plain gift |
| `present_offered` | a present appears for the pet | prize index |
| `location` | travel changes location | 0 home, 1 garden |
| `night` / `morning` | the pet's clock crosses the authored night bounds | — |
| `activity_started` / `activity_finished` | a moment or care activity starts or completes | activity ID |
| `need_low` | a need falls below the authored threshold (edge, not level) | need index |
| `potty_urge` | the potty urge crosses its threshold (edge) | — |
| `touched` | a touch reaction | reaction level |
| `woke` | a real wake transition | wake mood |

Stimuli go into a fixed queue of 8 per game. The queue isn't saved, and stimuli
arriving while it is full are dropped and counted. The queue drains once per
behaviour second (10 simulated ticks) for the active pet only. During offline
resume it is cleared without selecting anything.

## Content: `content/behaviors.json`

```json
{
  "version": 1,
  "night": {"start_minute": 1200, "end_minute": 360},
  "need_low": 250,
  "states": [
    {"name": "studying", "duration_s": [20, 45], "cooldown_s": 120,
     "effects": {"amusement": 15, "energy": -10}, "ends_on": ["touched", "activity_started"]},
    {"name": "asking_help", "duration_s": [30, 60], "cooldown_s": 180,
     "request": {"command": "feed", "bond": 20}, "ends_on": []}
  ],
  "repertoires": [
    {"name": "bubble", "affinities": [{"activity": "reading", "bonus_pct": 50}],
     "reactions": [
       {"on": "present_caught", "state": "curious", "weight": 3},
       {"on": "activity_finished", "value": "reading", "state": "studying", "chance_pct": 80},
       {"on": "need_low", "value": "satiety", "state": "asking_help"},
       {"on": "idle", "state": "contemplating", "chance_pct": 2, "when": {"night": true}}
     ]}
  ]
}
```

- **States** set a duration range, a cooldown, need and bond deltas applied on
  entry (bounded to ±100, then clamped to 0..1000), the stimuli that end them
  early, and an optional request.
- **Requests** name the command that resolves them (feed, play, clean, care,
  water, or a specific moment) and a bond reward. A matching successful command
  resolves the request, ends the state and starts its cooldown. Timing out has
  no penalty.
- **Reactions** match a stimulus kind, an optional value, and optional `when`
  conditions: location, night, mood range, and minimum bond. They pick a state
  with a weight and a chance. Selection is weighted among the matching reactions
  whose states are off cooldown, followed by a chance roll. Both draws use the
  pet's saved PRNG, so seeded replay never depends on frame rate. A stimulus
  listed in the current state's `ends_on` ends that state first.
- **Affinities** scale an activity's need gains for this repertoire, so BUBBLE
  enjoys reading more than Mint does.
- Each catalog form in `content/pets.json` may name a `repertoire`. Forms
  without one never enter states.

`cmake/JelliBehaviors.cmake` validates the file and generates bounded tables,
as the other core content files do. It needs no Python, so the core-only build
stays toolchain-light. Capacities: 16 states, 8 repertoires, 32 reactions each,
and 4 affinities each.

## Activities: `content/activities.json`

Moments become data. Each one has an ID, a label, an icon asset key, a kind
(`feed`, or `play` with a duration), need effects, an optional location
change, and an optional prop that is shown during the activity. The existing
four moments keep their IDs and effects, and a test compares them with the
former code. **Reading** (ID 4) is a play-kind moment with amusement and a
little social gain. It shows a book prop and takes the free sixth slot of the
Activities ring.

## Potty cycle

The user asked for "a natural potty cycle that is triggered from eating and
drinking", with a potty break in the Health ring's free sixth slot. Each meal and
drink adds to a per-pet `digesting` pool. The pool drains into a `potty` urge
(0..1000) at an authored rate per simulated minute, so the urge arrives a while
after the meal. When the urge crosses its threshold, a `potty_urge` stimulus fires,
and a repertoire can answer it with a request, such as BUBBLE asking for help.
POTTY is a short tap-to-finish health routine: it clears the urge and gives a
little hygiene. There are no accidents or penalties. The rates (amount per meal
and per drink, drain per minute, threshold, hygiene gain) live in
`content/potty.json`, with optional per-species multipliers on the repertoire.
This adds 4 bytes per pet to the save (`digesting`, `potty`).

## Saved state (save version 10)

Each pet saves `digesting` and `potty` (u16 each), plus `behavior_state` (u8, 0 = none), `behavior_remaining_s` (u16),
`behavior_request` (u8), `cooldown_state` (u8) and `cooldown_remaining_s` (u16):
12 bytes, or 108 for nine pets. The nine-pet save is about 3,605 of 4,096 bytes.
This fits, but the remaining headroom (about 420 bytes) should be watched.
Per-state cooldowns are not saved; only the most recent state's cooldown is.
Older saves load with no state.

## Presentation (pet layer)

- `assets.json` gains `state_poses`, such as `{"name": "study", "fallback": "curious"}`.
  A form may give a clip for a state pose; otherwise its fallback clip plays.
  The axolotl uses existing frames for now.
- `content/creatures.json` gains `state_presentation`, which maps each state to
  a pose, a caption ("STUDYING", "HMM...", "HELP?") and an optional effect
  sprite key, drawn above the head. For a request, the caption shows the action
  icon.
- The pose rules gain a `behaving` condition, so asleep and eating still win
  over a behaviour state.

## Magic-number scrub

An inventory of literals in `core/` drives this work. Asset IDs used in code
become named constants generated from `assets.json` keys. Creature tuning
(touch-load thresholds, reaction durations, preference windows, bond steps)
moves to content files. Reaction and wake codes become enums. Layout
coordinates become named constants in one layout header. Each step is tested
for unchanged behaviour.

## Authoring

Jelli Art gains a Behaviour view. It edits states, repertoires, reactions and
affinities, and includes a stimulus simulator: pick a stimulus, conditions and
a seed, and see which state is chosen and how it looks.

# Implementation order

1. Activities as data, plus Reading with a book icon and prop (placeholder art).
   Then the potty cycle and the POTTY routine.
2. Behaviour engine: stimuli, states, repertoires, requests, save v10, and
   BUBBLE's repertoire.
3. Presentation: state poses with fallback, captions and effect sprites.
4. Magic-number scrub, in tested slices.
5. Jelli Art behaviour authoring.

# Alternatives Considered

- **A behaviour tree or scripting language.** RFC-001 already rejects these: they
  cost too much before needs justify them, and bounded validation is harder.
- **Purely cosmetic states.** The user chose light effects and requests instead.
- **Simulating states offline.** Rejected: it adds deterministic replay cost and
  makes outcomes the player never sees.

# Open Questions

- Whether request icons need new art, or reuse the ring icons.
- Whether night bounds should follow each pet's bedtime instead of global minutes.

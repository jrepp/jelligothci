---
title: Activity costs, rewards, pet balance and location integrity
author: Codex
created: 2026-10-10T16:36:32Z
tags: [activities, authoring, balance, memo, testing]
id: memo-041
project_id: jelligotchi
doc_uuid: 527158e3-36d2-480c-818a-4128ba989d34
---

# Findings and requested changes

The user requested an audit of activity gains/costs, stronger meter impact,
random variation and pet bonuses. Follow-up requirements distinguish slow bond
growth from ordinary needs, Mint's higher energy use and age restrictions,
Axolotl's higher hydration use, randomized allowed locations, and build-time
cross-reference integrity. The user confirmed locking chess, science and fishing
until Mint evolves; Lilac and Axolotl can perform them. Other numerical values
below are initial playtest tuning, not an approved final balance.

Before this change every play recipe inherited +25 fun, −5 energy and −2
hygiene. Extra recipe gains and favorite rewards arrived immediately at start,
so interrupting Reading or Tea retained the bonuses. Meals inherited ordinary
Meal food's +30 fullness, −1 hygiene and one food consumed at completion.
These hidden shared effects made very different recipes similar and let
interrupted activities grant unearned rewards.

# Complete activity audit

All numbers use the displayed 0–100 meter scale; content stores tenths of a
point (0–1000). Previous values exclude favorites, species affinity, habits and
passive decay. New gains are nominal rewards before variation/bonuses and
caps; new costs are before per-pet multipliers. All four meals cost one food
both before and after this change. Location lists select from the current
Home/Garden scenes; no additional scene artwork is implied.

| Activity | Previous gains and costs | Provides on completion | Consumes at start | Pet reward bonus | Allowed locations |
| --- | --- | --- | --- | --- | --- |
| BREAKFAST | +30 fullness; −1 hygiene | +35 fullness, +10 energy, +12 hydration, +0.1 bond | −2 hygiene; 1 food | — | home, garden |
| TEA | +6 connection, +10 energy, +25 fun; −5 energy, −2 hygiene | +18 energy, +8 fun, +22 connection, +25 hydration, +0.2 bond | −3 fullness, −1 hygiene | Lilac +30% | home, garden |
| GOING OUT | +25 fun; −5 energy, −2 hygiene | +26 fun, +18 connection, +0.2 bond | −6 fullness, −10 energy, −6 hygiene, −8 hydration | Mint +25% | garden |
| MOVIE | +10 connection, +25 fun; −5 energy, −2 hygiene | +30 fun, +18 connection, +0.2 bond | −4 fullness, −4 energy, −6 hydration | Lilac +30% | home |
| READING | +31 fun, +3 connection; −5 energy, −2 hygiene | +22 fun, +8 connection, +0.2 bond | −6 energy, −6 hydration | Axolotl +50% | home, garden |
| LUNCH | +30 fullness; −1 hygiene | +40 fullness, +10 energy, +15 hydration, +0.1 bond | −2 hygiene; 1 food | — | home, garden |
| DINNER | +30 fullness; −1 hygiene | +45 fullness, +12 energy, +8 connection, +15 hydration, +0.2 bond | −3 hygiene; 1 food | — | home, garden |
| DESSERT | +30 fullness; −1 hygiene | +12 fullness, +18 fun, +0.1 bond | −4 hygiene; 1 food | — | home, garden |
| YOGA | +10 energy, +25 fun; −5 energy, −2 hygiene | +22 energy, +12 fun, +6 connection, +0.1 bond | −3 fullness, −6 hydration | Lilac +30% | home, garden |
| JOGGING | +25 fun; −5 energy, −2 hygiene | +30 fun, +8 connection, +0.2 bond | −10 fullness, −16 energy, −10 hygiene, −16 hydration | Mint +35% | garden |
| FISHING | +25 fun; −5 energy, −2 hygiene | +26 fun, +10 connection, +0.2 bond | −6 fullness, −8 energy, −6 hygiene, −6 hydration | Axolotl +30% | garden |
| DAY DREAMING | +3 connection, +25 fun; −5 energy, −2 hygiene | +12 energy, +18 fun, +0.1 bond | −2 fullness, −6 hydration | Lilac +25% | home, garden |
| NAP | +20 energy, +25 fun; −5 energy, −2 hygiene | +35 energy, +6 fun | −4 fullness, −6 hydration | — | home |
| CHESS | +35 fun; −5 energy, −2 hygiene | +28 fun, +16 connection, +0.2 bond | −9 energy, −6 hydration | Axolotl +35% | home |
| DRAWING | +33 fun; −5 energy, −2 hygiene | +28 fun, +10 connection, +0.2 bond | −6 energy, −3 hygiene, −6 hydration | Lilac +35% | home, garden |
| THINKING | +4 connection, +25 fun; −5 energy, −2 hygiene | +22 fun, +6 connection, +0.2 bond | −6 energy, −6 hydration | Axolotl +35% | home, garden |
| GARDENING | +33 fun; −5 energy, −2 hygiene | +26 fun, +10 connection, +0.2 bond | −6 fullness, −10 energy, −16 hygiene, −10 hydration | Mint +30% | garden |
| SOCCER | +35 fun; −5 energy, −2 hygiene | +32 fun, +18 connection, +0.2 bond | −10 fullness, −16 energy, −12 hygiene, −16 hydration | Mint +35% | garden |
| VOLLEYBALL | +10 connection, +25 fun; −5 energy, −2 hygiene | +26 fun, +26 connection, +0.2 bond | −8 fullness, −14 energy, −8 hygiene, −14 hydration | Lilac +30% | garden |
| SWIMMING | +6 energy, +25 fun; −5 energy, −2 hygiene | +32 fun, +12 connection, +8 hygiene, +0.2 bond | −10 fullness, −18 energy, −18 hydration | Axolotl +40% | garden |
| BASEBALL | +35 fun; −5 energy, −2 hygiene | +30 fun, +20 connection, +0.2 bond | −10 fullness, −15 energy, −10 hygiene, −15 hydration | Mint +30% | garden |
| BUG CATCHING | +33 fun; −5 energy, −2 hygiene | +28 fun, +10 connection, +0.2 bond | −6 fullness, −10 energy, −10 hygiene, −8 hydration | Mint +35% | garden |
| SCIENCE | +33 fun; −5 energy, −2 hygiene | +30 fun, +12 connection, +0.2 bond | −10 energy, −7 hygiene, −6 hydration | Axolotl +40% | home |

Exercise remains a separately authored workout in `content/exercise.json`:
+20 fun at completion, −15 fullness, −5 energy and −20 hydration at start.
It now shares the per-pet cost multipliers. Generic Play retains its +25 fun,
−5 energy and −2 hygiene completion effects. Ordinary Feed remains available
outside timed meals and uses `content/food.json`: Meal +30 fullness; Fruit
+15 fullness/+10 hydration; Soup +22 fullness/+25 hydration, with one food
and −1 hygiene each. Water fills hydration. These care actions remain the
fallback when named activities are locked or resources are low.

# Runtime rules and slow progression

- Costs are fixed, checked together and paid once after successful acceptance.
  A rejected command/preflight does not change meters, inventory or PRNG state.
  Interrupted activities retain costs and receive no recipe or favorite reward.
- Gains arrive at completion with one bounded ±15% roll per recipe. Authors may
  select 0–25% jitter. The roll is a hash of saved pet identity, activity ID and
  deadline, so redraws, preflight and reloads cannot reroll an accepted activity.
- Pet bonuses multiply positive recipe rewards only. Explicit activity bonuses
  override the older behavior affinity; they never stack with it. The catalog
  explicitly specifies all three forms, including zero bonuses. Bubble's +50%
  Reading affinity is preserved through its explicit activity bonus.
- Mint pays 130% energy; Lilac pays base costs; Axolotl pays 150% hydration.
  These activity-wide form profiles are editable. Costs round upward to a tenth
  of a point; reward calculations round to the nearest tenth. For example,
  Reading costs Mint 7.8 energy/6 hydration and Axolotl 6 energy/9 hydration.
  Jogging costs Mint 20.8 energy/16 hydration. Swimming costs Axolotl
  18 energy/27 hydration. Passive need decay rates are unchanged.
- Bond uses base rewards of 0.1–0.2 points, with small pet/jitter bonuses.
  Favorites add only 0.1 bond (formerly 1), plus their existing authored
  +3 or +6 fun and half as much connection. Favorites now reward completion.
  Normal touch gives 0.3 bond, health care 0.5, gifts 2.5 and answered requests
  1–2; these existing, separate interactions are unchanged. Bond has no passive
  decay. It should accumulate over repeated care rather than refill like energy.
- Existing sleep habits still scale fun/connection gains by 80%, 100% or 120%.
  Passive decay continues during activities. Meters saturate at 100. Costs
  respect recovering pets' 40-point need floor; they cannot spend protected
  recovery reserves. No generic Play/Feed effects stack onto named recipes.
- A started activity can complete once during offline catch-up using its paid
  costs and saved reward roll. Offline completion does not unlock prerequisites.
  Dessert still requires a completed dinner that local day.

# Locations, authoring and integrity

`content/locations.json` is the shared location catalog. Activities specify a
nonempty set of allowed keys and whether to randomize. An accepted activity
chooses a deterministic random allowed place, or stays in its current allowed
place when randomization is off, otherwise travels to the first allowed catalog
place. The chosen location is saved immediately. Location transitions still
emit existing behavior stimuli. Age, resources and time are checked before
travel or costs. Places are currently stable IDs 0/Home and 1/Garden; adding
another requires renderer/travel support and an explicit catalog extension.

Jelli Art exposes gains, costs, jitter, per-form reward bonuses, global pet
energy/hydration multipliers, allowed locations and randomization. Its per-pet
preview shows reward ranges and actual costs in meter points. Build-time CMake
checks run for desktop, core-only and ESP32: unknown/duplicate locations,
unknown forms or assets, duplicate pet bonus/cost profiles, missing form cost
profiles, invalid prerequisite IDs/cycles, unknown favorite/behavior activities,
and out-of-range effects are rejected. The activity generator and behavior
generator read the same location catalog. Inputs are configure dependencies,
so incremental builds revalidate changed content. Jelli Art uses those same
generators before saving and retains stale-write protection.

# Persistence and resource budget

Save version 13 changes reward timing without adding serialized fields. Old
in-flight named recipes are retired on load because they may already have
received immediate rewards under versions 10–12. Other state and ordinary
in-flight care are retained. Current running recipes resume normally.
No new mutable per-pet/game state or core allocation is required: the saved
deadline fixes the roll and the saved location fixes travel. Gains/costs and
bounded bonus/profile arrays are read-only generated data. Runtime work is
bounded by seven meters, eight bonuses and eight form profiles. Measured host
sizes remain 400 bytes per pet and 4,080 bytes per game; each new recipe is
96 bytes and each pet cost profile is 6 bytes. No save payload bytes were added.

# Validation and remaining uncertainty

Validation passed: 26 core tests; all 54 desktop tests and 54 sanitizer tests
passed across the full run and targeted rerun of the two modified regressions;
`make hooks-check` (format, clang-tidy, Cppcheck, size gates and docs);
`make esp-build`; and documentation check/fix. The documentation fix only sorted
the new memo's tags. New tests cover paid-once costs, rejection/preflight,
interruption, saved jitter, offline completion without unlocks, pet modifiers,
age restrictions, location selection, slow bond and pre-v13 migration. Content
mutation tests reject broken locations, forms, assets, activity prerequisites,
favorites and invalid meter/bonus/cost rules. Existing nine-pet save checks pass.

The first pass exposed two test issues: a legacy assertion compared rounded
mood instead of the actual favorite-bonus meters, and new no-op tests compared
padded C objects. Tests now compare the affected meters and explicit serialized
state. No compiler or analysis diagnostics were suppressed.

Safari editing against an isolated content copy verified per-pet cost/reward
previews and persisted changes to hydration costs, bonuses and location
randomization. Axolotl Reading preview correctly changed from 9 hydration to
12 when its base cost changed from 6 to 8; Mint's energy cost remained 7.8.
The SDL demo snapshot was inspected. The temporary server and browser tab were
closed. Numerical balance still needs playtesting; deterministic tests do not
establish that pacing is fun. Hardware remains unflashed and unverified.

# References

- [Prior activity implementation](memo-040-authored-activity-unlocks.md)
- [Jelli Art authoring workflow](../../tools/jelli-art/README.md)
- [Behavior proposal](../rfcs/rfc-005-stimulus-driven-creature-behaviour.md)
---
title: Pet-led interaction and activity discovery
status: Draft
author: Codex
created: 2026-11-13T18:00:15Z
tags: [activities, gameplay, interaction, rfc]
id: rfc-013
project_id: jelligotchi
doc_uuid: 981f2572-772e-419c-bd39-196e4e375759
---

# Summary

Make the pet the entry point for interaction and the messenger for newly
available activities. A thought bubble invites the player to check in; tapping
the pet opens the main wheel; an animated Activities item leads to the special
and timed activities that are ready now. Holding the pet for three seconds
opens Settings. Keep the bottom back control while a wheel is open. Persist
pending opportunities and retire expired ones on wake before presenting cues.
Relax selected activation rules so pet-generated invitations become a primary
way to access more activities, with the Play/Activities menu retained for browsing.

This RFC records the owner's playtest direction on 2026-11-13 and subsequent
request to implement and validate the complete proposal. The requested behavior
is the implementation scope. Initial tuning choices are documented in
[memo-115](../memos/memo-115-pet-led-activity-discovery.md); they remain subject to
playtesting rather than being presented as separately approved balance defaults.

# Motivation

Time windows and chance-based availability create variety, but players have
little indication that something has become available. Discovery currently
requires searching menus. Letting the pet visibly invite an interaction makes
checking in feel like responding to the creature and gives those existing
opportunities a clear route into play.

# Requested behavior

- Replace the idle scene's MENU button with tapping the pet to open the main
  wheel. Opening navigation replaces the tap's comfort effect.
- Hold the pet for three seconds to open Settings directly.
- Retain the bottom back control while the wheel is open: return one level
  from a submenu, and close the wheel at its root.
- Give the pet an event generator that signals newly available activities
  through a small thought bubble.
- When the player opens the main wheel, give Activities a little attract
  animation cycle, leading to the unique and timed activities currently ready.
- Relax selected activity activation rules for pet-generated invitations, so
  more activities are accessible through interacting with the pet instead of
  requiring discovery through the Play/Activities menu.
- Keep the player in control of starting those activities.
- Persist pending activities across saves and restarts, and reconcile retirement
  on wake. Here, pending means an offered opportunity, not an activity already
  running. Retirement is interpreted as removing expired or invalid offers;
  waking alone does not discard a still-valid offer.

# Current implementation and related design

The working checkout's [activity rules](../../core/activities.c) already expose
window, form, prerequisite, random-choice and cost eligibility. Random selection
is deterministic for the pet and time bucket; checking availability does not
reroll it. [Activity content](../../content/activities.json) owns the catalog
and authored windows. Full [action preflight](../../core/pet_actions.c) checks
whether a command can actually start.

[Scene taps](../../core/pet_ui.c) currently dispatch to scene objects and the
pet before menu controls. The original sprite-mask pet interaction sent touch or wake commands. The
[implemented pointer path](../../core/pet_pointer.c) now uses a forgiving target
and opens navigation without those effects, including while the pet sleeps.
Existing routine-specific wake input remains in
[pet actor handling](../../core/pet_actor.c). Deployment and validation evidence
are tracked separately in memo-115.

[Memo-040](../memos/memo-040-authored-activity-unlocks.md) records authored unlocks;
[memo-108](../memos/memo-108-gameplay-and-touch-latency-review.md) identifies
activity pagination and disabled-action comprehension problems.
[RFC-005](rfc-005-stimulus-driven-creature-behaviour.md) supplies related creature
stimulus concepts. This proposal does not authorize its entire behavior system.
It revises the MENU-entry direction in the earlier draft
[PRD-001](../prd/prd-001-moments-and-readable-care.md); that record remains historical.

# Proposed interaction details

## Pet gestures

Recognize a tap on release and a hold once at the three-second threshold.
Show immediate pressed feedback and visible hold progress so Settings is
learnable. A completed hold consumes release: it must never also open the main
wheel or grant comfort. Movement beyond the gesture tolerance, cancellation,
focus loss, scene transitions and switching pets cancel the pending gesture.
Use injected monotonic time per [ADR-002](../adr/adr-002-inject-time-and-keep-pacing-in-hosts.md).

Use a forgiving pet target rather than requiring opaque sprite pixels. Keep
its position stable during a gesture. Menu controls and active minigame/routine
input retain ownership when those experiences are open; pet navigation applies
to the ordinary pet scene. Existing present and mess targets need explicit
overlap tests so a pet gesture cannot also collect or clean something.

Proposed sleeping behavior: tapping opens navigation without waking the pet;
Wake remains an explicit action. Retain the existing Settings menu route and
swipe navigation as alternative access. Whether comfort needs its own explicit
care action remains open.

## Opportunity events

Generate pet invitations that can grant access under relaxed activation rules,
as well as signal opportunities already available. The owner's follow-up expands
the scope beyond the earlier discovery-only proposal. The pet → main wheel →
Activities path remains the interaction flow; the pet can now offer something
that would otherwise be gated in the ordinary catalog. Tapping the pet still
opens navigation, never automatically starts an activity.

Separate eligibility to generate an invitation from eligibility to accept a
persisted invitation. Use authored per-activity policies for which restrictions
an invitation may relax. Proposed first candidates are the hourly random-choice
gate and general leisure time windows. Time-of-day can influence selection
without making every leisure activity inaccessible outside its usual window.
For example, a proposed Fishing invitation could remain actionable even when
Fishing is not the catalog's current hourly random choice.

| Rule | Proposed treatment for pet invitations |
| --- | --- |
| Hourly random choice | Select through the pet event generator; a valid invitation satisfies this gate without another roll at activation |
| General leisure windows | Allow designated activities outside their catalog window for the invitation's authored lifetime |
| Theme-defining windows, such as named meals | Preserve unless explicitly relaxed for that activity |
| Form restrictions and prerequisites | Preserve initially; review specific restrictions individually |
| Awake/idle state and ability to pay costs | Recheck at activation; temporarily blocked offers can remain pending |
| Costs, rewards and daily reward limits | Preserve existing accounting; invitation access does not grant extra payouts |

These are proposed rule categories, not approval of individual activity tuning.
The initial activity set, invitation frequency, lifetime and cooldowns are
recorded in the implementation sources linked from memo-115. Use bounded generation with persisted selection and
cooldown state so menu opening, pet tapping, reload and repeated wake cannot
farm new rolls. Offering more activities should improve access without flooding
the pet with prompts; pending capacity and replacement policy need explicit tuning.

Both presentation and execution must use the same invitation-aware preflight.
Validate the pending offer against its pet, activity, expiry and authored policy;
a caller-supplied flag must not bypass restrictions. Ordinary catalog activation
continues to use its rules unless it resolves the same valid pending invitation.
Evaluate opportunities after relevant clock, pet, prerequisite and resource
changes. Only advertise an activity as ready when its activation preflight succeeds.

Track an opportunity by pet identity, activity identity and its availability
window or generated offer instance. Distinguish a newly available opportunity from a
previously seen one temporarily blocked by being busy or asleep. Returning to
idle must not repeatedly announce the same opportunity. A newly satisfied
prerequisite or recovered resource can expose an unseen opportunity, while
expiration removes it even if it was never inspected.

Proposed lifecycle: newly ready and unseen; shown in Activities and acknowledged;
started or expired. Opening the root wheel alone does not acknowledge an
opportunity. Acknowledgement happens when its entry is actually presented in
the Activities view; off-page items remain unseen. Starting an activity uses the
shared command path and rechecks invitation-aware eligibility. A persisted
invitation retains its specific access grant until expiry; the visual cue itself
never pays a cost or grants a reward.

## Persistence and retirement on wake

The owner requires pending activities to persist and retirement to be handled
on wake. Persist each pending opportunity's pet identity, stable activity
identity, offer identity, origin/policy identity and expiry basis. Persist the
selection and cooldown state needed to prevent regeneration on reload. Proposed additional
state includes whether the offer has been seen, so restarting does not turn an
acknowledged invitation into a new notification. Animation phase and gesture
state remain transient. The precise bounded record layout is an implementation
decision. The saved offer identifies the authored access policy; it must not
serialize an unrestricted bypass of future validation.

Reconcile saved opportunities after loading and offline progression, on device
wake or foreground resume, and when the pet wakes. Display wake may occur while
simulation has continued; reconciliation must work without requiring a reload.
Perform reconciliation before rebuilding the thought bubble, attract marker or
ready list:

1. Restore pending records and establish the current gameplay clock using the
   existing time policy.
2. Retire offers whose availability period has ended or whose pet/activity
   identity or access policy is no longer valid. An offer expires at its own
   closing boundary. For an explicitly relaxed window, the original catalog
   window closing or hourly selection changing does not prematurely retire it.
3. Retain valid offers and their seen state. Being temporarily busy, asleep or
   unable to pay a cost hides readiness; it does not by itself retire an offer.
4. Evaluate currently available opportunities and merge by stable identity,
   without duplicating retained offers or replaying missed windows. Resume alone
   does not grant an extra generation roll; honor persisted generator timing.
5. Persist lifecycle changes through the existing save path and publish the
   reconciled presentation state.

Starting an activity consumes its pending offer only when the command succeeds.
Retirement grants no reward, charges no cost and does not count as completion.
Retain bounded deduplication state for the relevant availability period so a
started or retired offer cannot be recreated as new by a repeated wake or reload.
Reconciliation must be idempotent; switching pets applies the same rules to that
pet without leaking another pet's notifications.

Expiry must use a save-stable gameplay time basis, not a monotonic timestamp
from a previous process. Clock corrections trigger reconciliation without
extending saved deadlines or minting rewards. The implementation policy for unknown time and rollback is recorded in memo-115: do not assume elapsed wall time
can be reconstructed when it cannot. The selected fallback must preserve
identity and deduplication and avoid presenting unverified timed offers as ready.

Older saves load with an empty pending-opportunity ledger and evaluate only
current opportunities after resume. Extend the versioned save codec with bounded
counts, validated identities and migration/corruption tests. Pending offers are
separate from in-progress activity or minigame checkpoints.

## Thought bubble and wheel

Show one small thought bubble near the pet when an unseen, actionable special
opportunity exists. Reuse its activity icon so the visual invitation matches
the destination. Several opportunities share one bubble, with a gentle icon
cycle; timing and motion amplitude require visual review. Prefer a brief attract
cycle followed by a quiet visible cue over uninterrupted flashing. Suppress
invitations during sleep, active routines, minigames and celebrations; recompute
when the ordinary scene returns. Ignoring a bubble has no added penalty.

When the main wheel opens, transfer the invitation to Activities with a brief
pulse or wiggle and a persistent small marker. Keep icons and hit targets fixed
while animating decoration. The bubble and marker represent the same unseen
opportunities; neither independently chooses or rerolls activities.

## Activities ready now

Lead with currently actionable special opportunities: timed, chance-selected,
pet-specific or prerequisite-unlocked activities. The owner's device feedback
refines this into one ready-only Activities view, including ordinary Play and
Exercise. Do not list unavailable activities when opening it. The confirmed
main-wheel layout has Food, Water and Care fixed on the right, with two ready
activity shortcuts and Activities on the left. Care opens individual health and
hygiene routines; remove the standalone Basic Care treatment shortcut. More and
the separate Play entry leave the top level. Settings retains pet management
and gifts through Pet Menu. A closed-scene downward swipe supplies an alternative
Settings route to the three-second hold. Use stable ordering within a displayed
page and a clear page indicator if more choices fit than the wheel can show.
Never silently replace a target under a held finger.

Identify timed opportunities with a small clock cue; use authored activity art
as the primary recognition aid. Exact countdowns and urgency language are not
required. If eligibility changes between display and activation, explain that
it has ended or is blocked and refresh the choices. Keep Back available and
show a calm empty state when no special opportunity is ready. Repeated free play
must not appear to offer a new unique reward.

# Implementation boundaries

Share a bounded availability projection between bubble, root marker and activity
choices. Extend shared command preflight to validate persisted invitations and
their authored rule relaxations rather than copying rules into rendering.
Keep detection and acknowledgement outside drawing so rendering frequency
cannot create events. Capacity follows the authored activity limits; no heap
allocation or unbounded event history is needed. Gesture timing and cue timing
use injected time, with platform input adapters reporting cancellation reliably.

Likely integration points are [UI state](../../include/jelli/pet_ui.h),
[gesture dispatch](../../core/pet_ui.c), [pet rendering](../../core/pet_draw.c),
[menu rendering](../../core/pet_menu.c), and the activity/preflight sources above.
Follow the [engine/host contracts](../../docs/development.md) during implementation.
Persistence and wake retirement are requested requirements. The implemented
save layout, migration, clock fallback and authored invitation policies are
described in memo-115, with validation evidence and remaining gaps.
The [save codec](../../core/save.c) owns serialization and compatibility.

# Tradeoffs and open decisions

The pet becomes a larger, more natural navigation target, but direct comforting
loses its current gesture and Settings needs a discoverable hold hint. A bubble
can make the pet expressive, but excessive prompting would make it demanding.
Ready-first presentation is simpler to explore, but changing eligibility requires
stable hit targets and honest feedback.

The owner has requested additional access through pet activation. Initial
implementation choices use persisted seen state, selective authored leisure
policies, a bounded generator and a stable ready-first order. Ordinary pet taps
open navigation even during sleep; Wake remains explicit. Comfort remains a
legacy engine command rather than gaining a new UI action in this increment.
Unknown-time fallback, cooldown and retirement behavior are recorded in memo-115.
Activity mix, invitation frequency and visual repetition remain playtest tuning
questions rather than missing activation mechanisms.

Keeping all existing gates and only adding badges would improve discovery but
would not satisfy the expanded access request. Removing all gates globally would
lose time-of-day character and progression; selective authored invitation policies
allow broader access while retaining meaningful activity differences.

# Adoption and acceptance

Implement in reviewable increments: pet navigation and hold handling; shared
invitation generation and shared activation policies with persistence and wake
retirement; bubble, wheel cue
and ready-first activities presentation.
Update the playing guide and entry-point instructions when behavior ships.

Validate short tap, release immediately below the hold threshold, exactly at
three seconds, continued hold and release, drag cancellation, lost focus,
sleeping pets, overlapping scene targets and active-game input ownership.
Validate that an authorized invitation can start an activity blocked by the
ordinary hourly-choice or relaxed leisure-window rule. Verify the same command
without a valid offer cannot bypass that rule, and that theme-specific windows,
form restrictions, prerequisites, costs and reward limits still apply unless
explicitly changed by authored policy. Verify offers survive an hourly selection
change until their own expiry and generation cannot be accelerated by tapping,
menu navigation or reload. Validate window boundaries, stable selection, prerequisite unlocks,
resource gating, expiry while a menu is open, busy-to-idle deduplication,
multiple opportunities, pet switching, clock corrections and resume. Cover
save/reload with unseen and seen offers, older-save migration, malformed records,
wake just before and at expiry, repeated wake, display-only wake, long offline
gaps, missing time and rollback. Verify valid pending offers survive restart,
expired ones never flash a stale cue, successful starts cannot regenerate the
same offer, and failed starts retain it. Verify
navigation never changes comfort, wakes the pet or starts an activity implicitly.

Run the changed-layer checks from [CONTRIBUTING](../../CONTRIBUTING.md), inspect
desktop renders, and separately verify physical touch and readability. A user
should be able to follow bubble → pet tap → Activities cue → ready activity
without searching disabled pages. Test that loop with the owner and intended
players; host tests or serial startup cannot establish comprehension or physical
responsiveness.

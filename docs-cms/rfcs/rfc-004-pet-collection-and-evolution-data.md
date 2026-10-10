---
id: rfc-004
title: Pet collection, evolution data, and present selection
status: Draft
author: Codex
created: 2026-10-09
tags: [collection, content, evolution, gameplay, ux]
project_id: jelligotchi
doc_uuid: d7e3eb7a-917c-424c-ae02-e71ac4a6bfee
---

# Summary

Replace the Settings pet-swap action with access to a nine-slot Pets collection.
Use the same 3×3 layout as Presents. Each slot holds one pet identity and its
set of possible evolutions. Content data defines placement, forms, unlock rules,
and player-facing hints; saved state records ownership and progress.

The user requested this direction and a way to deselect a held present on the
small touch display. The details below are a draft design. This change adds
project documentation initially. The user later authorized implementation;
[memo-020](../memos/memo-020-pet-collection-and-present-controls.md) records the
implemented subset, including the initial shared two-form catalog. The broader
limits and generic schema below remain a draft proposal.

# Motivation

The current Settings action switches between the first two pet records. Players
need a visible collection that can grow through unlocks, with a clear distinction
between acquiring a pet and evolving a pet they already own. The current Presents
grid supplies a familiar layout on the 466×466 round panel.

A held present currently exposes a direct Give action. Deselecting it needs an
explicit touch control that works without a keyboard, precise gesture, or tiny
close icon.

# Detailed Design

## Collection access and slot behavior

Keep the entry in Settings and label it PETS. Opening it never switches the active
pet. Keep Presents as a separate collection with its existing nine prize types.
Both screens share the grid geometry, slot spacing, highlight, and Back/Close
placement. Their item actions remain distinct.

Use nine fixed positions in row-major order. A slot is an authored collection
entry, not the position of a record in a save array. One entry permits one owned
pet instance. Evolution changes that instance within its existing slot; it never
consumes another slot. Duplicate pets, release, breeding, and slot trading are
outside this proposal.

| Slot state | Grid display | Tap result |
| --- | --- | --- |
| Empty | Quiet empty frame | No action; reserved for future content |
| Locked | Silhouette and lock badge | Detail shows the unlock hint and progress |
| Owned, stored | Current form portrait | Detail shows the pet and evolution set |
| Owned, active | Current form portrait and ACTIVE badge | Same detail, with current status |
| Newly unlocked | Owned portrait and NEW badge | Detail clears the notice after viewing |

Active and new are independent flags on an owned entry. Locked entries remain
visible; empty entries have no pet or unlock rule. Slot positions never compact
when new content is added. Avoid names inside the small grid cells: show the
focused name below the grid, as Presents does, and full text on the detail page.
Use badges and silhouettes as well as color to communicate state.

The pet detail page has a large portrait, name, evolution progress, and one
primary BRING OUT action. The active pet instead shows ACTIVE with no switch
action. An EVOLUTIONS action opens the form set; Back returns to the same grid
position. Browsing never changes a pet's identity, form, or active status.

BRING OUT selects the saved instance, with the same sleep and activity guards as
normal pet activation. During linked sleep or a blocking interaction, disable
activation and show a short reason; still allow browsing. Switching preserves
needs, age, history, and unfinished state. Stored pets stay frozen under the
[RFC-001](rfc-001-virtual-pet-systems-architecture.md) clock contract. Exactly one
owned pet is active. Unlocking a pet does not activate it automatically.

## Evolution sets

Each entry references a bounded set of forms and directed evolution edges. Show
current, previously reached, and unreached forms distinctly. The detail view may
reuse a 3×3 grid for up to nine forms, with current/reached badges and silhouettes
for unreached forms. Selecting a form shows its hint and incoming conditions;
it does not equip that form or reverse an evolution.

A reached-form count measures discovery within this pet's set, not the number of
pets owned. Branches are alternatives. A pet may finish one branch without
reaching every form; do not promise 100% discovery on one irreversible life path.
Replay or rebirth would need a separate design.

Use RFC-001's acyclic evolution graph, stable rule IDs, priority ordering, and
fallback for nonterminal forms. Evaluate rules from a single care snapshot.
Changing form preserves pet identity, collection entry, unlock history, lifelong
memory, and present provenance. Only explicitly stage-local counters reset.

## Authored data and saved state

Keep immutable content separate from mutable progress. Stable IDs are explicit,
nonzero integers; array order and display position never define identity. Example
IDs below are illustrative allocations, not additions to the live catalogs.

| Record | Authored fields | Saved fields |
| --- | --- | --- |
| Collection layout | Layout ID, rows, columns, nine slot-to-entry bindings | Optional last focused entry |
| Pet entry | Entry ID, label, starting form, evolution set, unlock rule | Entry ID to pet instance ID binding, seen notice |
| Evolution set | Set ID, form IDs, directed rules, form display order | Reached form IDs for the owned pet |
| Form | Form ID, label, portrait asset, stage, profile reference | Current form ID and stage-local state |
| Pet unlock rule | Rule ID, bounded conditions, progress hint | Counters and one-time grant latch |
| Pet instance | Initial profile and memory defaults | Stable identity, needs, clocks, care history, memory |
| Player collection | Starting entries | Active instance ID, unlock history, content/save versions |

A slot assignment with a null entry is empty. An assigned entry is locked until
its rule grants ownership. No second saved boolean should disagree with an
existing instance binding: derive ownership from that binding. Unlock history
records the one-time grant, even when content rules later change.

Illustrative authored JSON follows. The short evolution set has two forms; a
larger set uses the same records. Slot 2 references another entry defined in the
full catalog; omitted records are deliberate, so this is a schema example, not
a loadable content pack.

```json
{
  "schema_version": 1,
  "layout": {
    "id": 4001,
    "rows": 3,
    "columns": 3,
    "slots": [4101, 4102, null, null, null, null, null, null, null]
  },
  "pet_entries": [
    {
      "id": 4101,
      "label": "Starter",
      "starting_form_id": 4201,
      "evolution_set_id": 4301,
      "unlock_rule_id": 4401
    }
  ],
  "evolution_sets": [
    {
      "id": 4301,
      "form_ids": [4201, 4202],
      "display_order": [4201, 4202],
      "rule_ids": [4501]
    }
  ],
  "unlock_rules": [
    {
      "id": 4401,
      "scope": "player",
      "all": [{"kind": "new_game"}],
      "grant_entry_id": 4101,
      "hint": "Your first companion"
    }
  ],
  "evolution_rules": [
    {
      "id": 4501,
      "from_form_id": 4201,
      "to_form_id": 4202,
      "priority": 0,
      "fallback": true,
      "all": [{"kind": "stage_age_ticks_at_least", "value": 600}],
      "hint": "Grow together"
    }
  ]
}
```

The 600-tick value illustrates explicit units (100 ms pet ticks), not a proposed
production growth rate. Form/profile/asset definitions are separate catalog
records. The initial release roster, names, artwork, and balance remain to be
authored; blank positions do not imply seven finished new pets.

## Unlock rules and progress

Unlock pets at player scope so progress can span active pets. Evolution conditions
remain instance-scoped. Use a small typed condition vocabulary: starting grant,
completed activity count, discovered present ID, reached form ID, and count of
owned pet entries. An `all` list combines at most four conditions; no scripts or
arbitrary expression language. Define each counter's unit, qualifying event, and
saturation limit in the content schema before implementation.

Example candidate: completing three full play activities unlocks a companion.
This is a balance example only. Count successful activity completions, never taps,
menu opens, rejected actions, or repeated medicine/shot use. Read present
conditions from persistent discovery history, so gifting or putting away a
present cannot remove progress. Reaching a form produces one discovery event
per pet and form.

Grant ownership once, create one instance with its entry's starting profile,
record the rule grant, and checkpoint those changes together. Repeated event
handling or loading cannot create duplicates. Unlocks never revoke owned pets
when conditions or content change. Show a NEW notice at the next safe UI point;
do not interrupt sleep, an activity, or a present action to force open the grid.
Reject rules whose prerequisites cannot be reached, including circular pet
unlock dependencies without a starting grant.

## Present deselection on the device

Use the held present itself as the touch target. The existing 80×80 target near
the top of the home scene opens a small action panel instead of gifting on the
first tap. Label the held state PRESENT, reserving CATCH for an offered reward.
An offered reward keeps its existing catch behavior.

The action panel shows the present name/icon and two large controls: GIVE and
PUT AWAY. Each target is at least 80×64 pixels, lies fully inside the usable round
panel, and is separated from its neighbor. Use the normal bottom Back control;
swipe down also returns without changing the held selection. No long press,
small X, or keyboard modifier is required.

| Action | Result |
| --- | --- |
| Tap held present | Open its action panel; no transfer |
| GIVE | Recheck recipient and item; transfer once if valid |
| PUT AWAY | Clear held selection and reservation; return home |
| Back or swipe down | Close the panel and keep the present selected |
| Tap selected item in Presents | Open the same action panel |
| Select a different owned present | Replace the held selection; retain both items |

PUT AWAY never consumes, drops, gifts, or removes an item. It remains available
when the pet is asleep, busy, or the present's original giver. GIVE can be disabled
with a readable reason in those states, but does not disable PUT AWAY. A failed
Give leaves the selection intact. After a successful Give, clear the selection
and close the panel. Keep the active pet visible or named so the recipient is
clear. Switching pets through Pets preserves the held selection, allowing the
existing give-to-another-pet flow.

Commit actions on touch release and consume the gesture that opened the panel;
the same tap must never activate a newly exposed GIVE button. Resolve the held
item and recipient again at execution time. If the item is no longer owned,
clear the stale selection and show a short notice. If a catch offer is covering
the home target, the selected item's cell in Presents still opens this panel.

Selection is transient UI state unless a later save design explicitly persists
it. Putting an item away does not require an inventory write. If reservations
become durable, clearing one must join the same checkpoint transaction. Closing
a menu or inspecting an evolution never silently gives away a held present.

## Bounds and migration strategy

Proposed limits: nine pet entries and owned instances, nine forms and sixteen
edges per evolution set, one unlock rule per entry, and four conditions per rule.
These are new design bounds, not the existing compiled capacities. Reject missing
references, duplicate IDs or slot bindings, out-of-set evolution edges, cycles,
invalid starts, unreachable forms, unsupported conditions, and limits exceeded.
Never silently truncate a catalog or an owned collection.

The current runtime has eight pet records and two initial pets; save codec v3
uses a bounded 4 KiB buffer. Nine visible slots alone do not expand either limit.
Future implementation must budget nine full pet records plus discovery and unlock
state, measure worst-case encoded size, and explicitly version the save migration.
Do not claim the extra state fits the current buffer without that measurement.

Migration preserves every existing instance ID, active pet, form, clock, care
history, and present origin reference. Bind the current two starter pets to
explicit entry IDs and mark them owned even if a new-game unlock would otherwise
lock the second one. Define a deterministic mapping for every supported legacy
record (up to eight), with unique compatible entries. If no mapping exists,
retain the original save and report incompatibility instead of dropping a pet.
Infer only the current reached form when older history is unavailable; do not
invent past care events or retroactive unlock counters. Saved discovery facts
may satisfy matching new rules once, under an explicit migration policy.

This proposal replaces RFC-001's eight-entry collection target with nine and
refines RFC-002's Settings pet selection into collection access. All other
ownership, clock, and save guarantees remain applicable. No public API changes
or content loader are implemented by this document.

# Drawbacks

Pet details and explicit Give confirmation add a tap. Nine stored pets and form
history need more save and RAM budget than the current two-pet slice. Fixed slots
cap the first catalog, and permanent evolution branches leave some forms
undiscovered unless a later replay design addresses them.

# Alternatives

A grid cell that switches pets immediately saves one tap, but makes browsing and
unlock hints awkward. A flat grid of forms loses the distinction between pet
identity and evolution. A long press to deselect presents saves screen space,
but hides the action; the explicit panel is easier to discover and use.

# Adoption Strategy

Review the data model and author the roster before implementing it. A future
increment adds bounded catalogs, save migration, collection views, and the
present panel. Update playing instructions when those behaviors actually ship.
No firmware build or physical verification is claimed for this design change.

Acceptance scenarios for that later increment:

- Opening Pets leaves the active pet unchanged; all nine positions remain stable.
- Locked details show progress; completing a rule grants exactly one stored pet.
- Evolving preserves the slot, identity, memories, and present provenance.
- Reload preserves ownership and progress; legacy pets migrate without loss.
- Browsing works during linked sleep; activation explains why it is unavailable.
- A held present survives a pet switch and can be given to a valid recipient.
- PUT AWAY works for asleep, busy, and original-giver pets without item loss.
- Back keeps the selection; the panel-opening tap cannot accidentally give.
- Both action targets remain readable and reachable on the physical round screen.

# Unresolved Questions

The user has requested collection access and touch deselection. The roster,
unlock thresholds, evolution branches, and any future replay mechanic still need
authored content. The limits, two-step Give interaction, and migration mapping
are proposed details to validate before runtime work.

# Future Possibilities

A later proposal may add more collection pages, replay, or portable pet memory
import. None are prerequisites for this nine-slot design.

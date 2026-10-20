---
title: Jelli Art creation workflow delivery
author: Codex
created: 2026-10-20T06:08:59Z
tags: [jelli-art, memo, tooling, validation]
id: memo-077
project_id: jelligotchi
doc_uuid: db9ea9a7-aaeb-4098-bf49-a93d0c0df232
---

# Scope

The user authorized implementation of each finding in [memo-076](memo-076-jelli-art-creation-workflow-review.md), with Sol implementers and independent Sol adversarial reviews. This increment adds browser creation and engine support for those assets; it does not replace the renderer with Jellipak or implement the broader draft resource-store proposals.

# Delivered workflows

- **New pet:** choose artwork and gameplay templates, canvas size, behavior, explicit scales, unlock prize, and a single-form evolution policy. The creator appends distinct frame, form, profile, collection and clip identities. It copies the selected template's activity costs, eligibility, bonuses and repertoire membership only after that policy is explicitly selected. Multi-stage evolution authoring still uses the repository catalog workflow.
- **Frames:** the existing frame/timeline editor works on newly created forms. Existing identities remain stable.
- **Activities:** daily cost/reward policy, custom pose and completion prize are visible and persisted. New activities explicitly start without custom pose or prize and with rewards on each run; the unchanged SNUGGLE recipe retains its authored policy.
- **Presents and other art:** create icons, props and present artwork. Present registration is explicit and appends to the data-driven prize registry; newly registered prizes can be awarded by authored activities.
- **Backgrounds:** create and paint a background and register its location with an explicit indoor/outdoor choice.
- **Game preview:** select the location, present state and first/repeated completion. The preview uses real engine commands, exact simulation ticks and the reward observer; the small activity thumbnail is labeled approximate.

Implementation: [creator](../../tools/jelli-art/creation.py), [creator UI](../../tools/jelli-art/creation.js), [activity editor](../../tools/jelli-art/activities.js), [preview](../../tools/jelli-art/game_preview.py), [prize generator](../../cmake/JelliPrizes.cmake), and [registry](../../content/prizes.json).

# Persistence and bounded runtime

Creation validates a scratch catalog with the real generators before replacing files. Shared writer locking, content/PNG base hashes, a final pre-write base recheck, and rollback protect against stale or failed creation. New-file paths cannot overwrite unregistered artwork. A successful save followed by a failed editor refresh remains visibly saved and offers a reload action.

Storage limits are defined in [game.h](../../include/jelli/game.h), [collection.h](../../include/jelli/collection.h), [prizes.h](../../include/jelli/prizes.h), and [save.h](../../include/jelli/save.h). Actual catalog counts are separate from storage capacities. New collection entries reconcile against already discovered unlock prizes when an existing save loads. Legacy identities and progression counters retain their prior meaning; appended prizes are activity-awarded. Save version 15 extends prize origins while reading version 14's single additional origin correctly.

Measured on the local desktop build: a full 16-family fixture encodes **6,406 bytes**; `sizeof(JelliGame)` is **6,912 bytes**, `JelliSave` **6,936 bytes**, `JelliPetUi` **8,376 bytes**, and `JelliPetEngine` **17,760 bytes**. The save buffer and explicit action workspace budget are 8,192 bytes. This is a deliberate increase to accommodate new authored families, not a memory optimization. ESP engine/session storage remains static, and device decoding uses caller-owned scratch. These host type measurements do not establish device stack high-water marks.

# Adversarial findings and remedies

| Observation | Remedy and verification |
| --- | --- |
| Registering New before shell DOM initialization left mode tabs empty | Guard shell synchronization until chrome exists; actual Safari startup and a browser regression verify New and existing tabs |
| Existing saves did not discover appended families whose prize was already owned | Reconcile appended entries after save validation; current and v14 fixture coverage |
| Compact collection cells could overflow with valid large portraits | Clamp display scale to cell size; full-capacity rendered controls and navigation test |
| New simulation completion path handled rewards but ordinary advance/live paths did not | Process the engine reward observer on all simulation ticks; completed activity regressions |
| Preview could accept concurrent Render requests during catalog refresh | Set busy before the awaited refresh |
| A numeric string could emit an octal C prize ID | Require JSON numbers and reject malformed registry fields; generator negative tests |
| Creator reload failure could hide the saved result | Visible saved-but-refresh-failed status and recovery; browser regression |
| External edits during scratch validation could be overwritten | Recheck bases immediately before writing; deterministic mutation-during-validation test |
| The new full-collection UI test failed because its fixture skipped present setup and layout settling | Correct the fixture callback and settle two frames; affected desktop/sanitizer cases passed, followed by the complete sanitizer suite in CI |
| Creation allocated IDs from the manifest alone, allowing a retired frame identity to be reused | Reuse the frame allocator's active/imported/retired ID and key reservations; hash reservation metadata with creation bases and recheck before publication. Regressions cover retired ID skipping, retired key rejection without writes, and a changed ledger during validation |
| The first reservation fingerprint included unrelated lint metadata, breaking the existing invalid-lint recovery test | Limit the fingerprint to the frame ledger and import specifications actually used by reservation lookup; preserve file confinement and the existing recovery contract. Include the creation backend suite in the Studio CI test loop |
| Windows compilation and 81 tests passed, but release-art verification still treated prize storage capacity as the catalog count | Verify present artwork against the actual prize registry, preserving byte-for-byte manifest checks and distinct registered artwork; derive release artwork notes from current catalogs |

# Validation evidence

The core suite passed **39/39**. The dedicated browser creation suite passed **6/6**, covering startup, painting/reload, 48-pixel pet creation, registered presents, backgrounds/locations, stale recovery and successful-save/failed-refresh handling. The engine preview suite passed **24** scenarios, including a newly authored pet. Runtime lint and explicit analysis of new test files passed. The initial desktop and sanitizer runs each passed 84/85 cases; after correcting the collection fixture, both affected tests passed in both builds. [CI subsequently passed the complete sanitizer suite, 85/85, and core suite, 39/39](https://github.com/jrepp/jelligothci/actions/runs/39380297429).

Safari validation used the requested local `~/d/safari-mcp` CLI against copied content in ignored `build/jelli-art-fixes/`. It created a pet and registered present and opened both in Paint, created/painted a background and location, and copied SNUGGLE into an activity with an explicit pose, daily policy and newly registered prize. Production art was not changed by these trials. A Safari helper timeout and later tab-ownership rejection required a fresh owned validation session; no action was forced against an unowned tab.

The final creation backend suite passed **15/15**, including retired-frame reservation and concurrent-ledger regressions. Luna independently reviewed the reservation fix before publication. After narrowing the metadata fingerprint, Luna reran the complete server suite (**36/36**) and creation suite (**15/15**) successfully.

The release-art regression is registered as `package_art` in CTest and passes locally. A real staging → ZIP → verification smoke test packaged 121 manifest images and 10 registered presents, preserving byte-for-byte checks for registered and unregistered art. [Release CI on `65846ab`](https://github.com/jrepp/jelligothci/actions/runs/39384980388) subsequently passed Windows tests (**82/82**) and produced its executable/DLL ZIP, then passed macOS tests (**86/86**) and produced the native, source-art and preview archives.

The ESP32 firmware compiled successfully with the generated registry. No firmware was flashed and no physical display, touch, heap, or stack measurement was performed. A subsequent user-requested professional creative UX pass continues in a separate increment.
